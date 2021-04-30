# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.

# coding: utf-8
# pylint: disable=no-member, invalid-name, protected-access, no-self-use
# pylint: disable=too-many-branches, too-many-arguments, no-self-use
# pylint: disable=too-many-lines, arguments-differ
"""Definition of various recurrent neural network cells."""
__all__ = ['RecurrentCell', 'HybridRecurrentCell',
           'RNNCell', 'LSTMCell', 'GRUCell',
           'SequentialRNNCell', 'HybridSequentialRNNCell', 'DropoutCell',
           'ModifierCell', 'ZoneoutCell', 'ResidualCell',
           'BidirectionalCell']

from ... import symbol, ndarray
from ...base import string_types, numeric_types, _as_list
from ..block import Block, HybridBlock
from ..utils import _indent
from .. import tensor_types
from ..nn import LeakyReLU


def _cells_state_info(cells, batch_size):
    return sum([c.state_info(batch_size) for c in cells], [])

def _cells_begin_state(cells, **kwargs):
    return sum([c.begin_state(**kwargs) for c in cells], [])

def _get_begin_state(cell, F, begin_state, inputs, batch_size):
    if begin_state is None:
        if F is ndarray:
            ctx = inputs.context if isinstance(inputs, tensor_types) else inputs[0].context
            with ctx:
                begin_state = cell.begin_state(func=F.zeros, batch_size=batch_size)
        else:
            begin_state = cell.begin_state(func=F.zeros, batch_size=batch_size)
    return begin_state

def _format_sequence(length, inputs, layout, merge, in_layout=None):
    assert inputs is not None, \
        "unroll(inputs=None) has been deprecated. " \
        "Please create input variables outside unroll."

    axis = layout.find('T')
    batch_axis = layout.find('N')
    batch_size = 0
    in_axis = in_layout.find('T') if in_layout is not None else axis
    if isinstance(inputs, symbol.Symbol):
        F = symbol
        if merge is False:
            assert len(inputs.list_outputs()) == 1, \
                "unroll doesn't allow grouped symbol as input. Please convert " \
                "to list with list(inputs) first or let unroll handle splitting."
            inputs = list(symbol.split(inputs, axis=in_axis, num_outputs=length,
                                       squeeze_axis=1))
    elif isinstance(inputs, ndarray.NDArray):
        F = ndarray
        batch_size = inputs.shape[batch_axis]
        if merge is False:
            assert length is None or length == inputs.shape[in_axis]
            inputs = _as_list(ndarray.split(inputs, axis=in_axis,
                                            num_outputs=inputs.shape[in_axis],
                                            squeeze_axis=1))
    else:
        assert length is None or len(inputs) == length
        if isinstance(inputs[0], symbol.Symbol):
            F = symbol
        else:
            F = ndarray
            batch_size = inputs[0].shape[0]
        if merge is True:
            inputs = F.stack(*inputs, axis=axis)
            in_axis = axis

    if isinstance(inputs, tensor_types) and axis != in_axis:
        inputs = F.swapaxes(inputs, dim1=axis, dim2=in_axis)

    return inputs, axis, F, batch_size

def _mask_sequence_variable_length(F, data, length, valid_length, time_axis, merge):
    assert valid_length is not None
    if not isinstance(data, tensor_types):
        data = F.stack(*data, axis=time_axis)
    outputs = F.SequenceMask(data, sequence_length=valid_length, use_sequence_length=True,
                             axis=time_axis)
    if not merge:
        outputs = _as_list(F.split(outputs, num_outputs=length, axis=time_axis,
                                   squeeze_axis=True))
    return outputs

class RecurrentCell(Block):
    """Abstract base class for RNN cells

    Parameters
    ----------
    prefix : str, optional
        Prefix for names of `Block`s
        (this prefix is also used for names of weights if `params` is `None`
        i.e. if `params` are being created and not reused)
    params : Parameter or None, default None
        Container for weight sharing between cells.
        A new Parameter container is created if `params` is `None`.
    """
    def __init__(self, prefix=None, params=None):
        super(RecurrentCell, self).__init__(prefix=prefix, params=params)
        self._modified = False
        self.reset()

    def reset(self):
        """Reset before re-using the cell for another graph."""
        self._init_counter = -1
        self._counter = -1
        for cell in self._children.values():
            cell.reset()

    def state_info(self, batch_size=0):
        """shape and layout information of states"""
        raise NotImplementedError()

    def begin_state(self, batch_size=0, func=ndarray.zeros, **kwargs):
        """Initial state for this cell.

        Parameters
        ----------
        func : callable, default symbol.zeros
            Function for creating initial state.

            For Symbol API, func can be `symbol.zeros`, `symbol.uniform`,
            `symbol.var etc`. Use `symbol.var` if you want to directly
            feed input as states.

            For NDArray API, func can be `ndarray.zeros`, `ndarray.ones`, etc.
        batch_size: int, default 0
            Only required for NDArray API. Size of the batch ('N' in layout)
            dimension of input.

        **kwargs :
            Additional keyword arguments passed to func. For example
            `mean`, `std`, `dtype`, etc.

        Returns
        -------
        states : nested list of Symbol
            Starting states for the first RNN step.
        """
        assert not self._modified, \
            "After applying modifier cells (e.g. ZoneoutCell) the base " \
            "cell cannot be called directly. Call the modifier cell instead."
        states = []
        for info in self.state_info(batch_size):
            self._init_counter += 1
            if info is not None:
                info.update(kwargs)
            else:
                info = kwargs
            state = func(name='%sbegin_state_%d'%(self._prefix, self._init_counter),
                         **info)
            states.append(state)
        return states

    def unroll(self, length, inputs, begin_state=None, layout='NTC', merge_outputs=None,
               valid_length=None):
        """Unrolls an RNN cell across time steps.

        Parameters
        ----------
        length : int
            Number of steps to unroll.
        inputs : Symbol, list of Symbol, or None
            If `inputs` is a single Symbol (usually the output
            of Embedding symbol), it should have shape
            (batch_size, length, ...) if `layout` is 'NTC',
            or (length, batch_size, ...) if `layout` is 'TNC'.

            If `inputs` is a list of symbols (usually output of
            previous unroll), they should all have shape
            (batch_size, ...).
        begin_state : nested list of Symbol, optional
            Input states created by `begin_state()`
            or output state of another cell.
            Created from `begin_state()` if `None`.
        layout : str, optional
            `layout` of input symbol. Only used if inputs
            is a single Symbol.
        merge_outputs : bool, optional
            If `False`, returns outputs as a list of Symbols.
            If `True`, concatenates output across time steps
            and returns a single symbol with shape
            (batch_size, length, ...) if layout is 'NTC',
            or (length, batch_size, ...) if layout is 'TNC'.
            If `None`, output whatever is faster.
        valid_length : Symbol, NDArray or None
            `valid_length` specifies the length of the sequences in the batch without padding.
            This option is especially useful for building sequence-to-sequence models where
            the input and output sequences would potentially be padded.
            If `valid_length` is None, all sequences are assumed to have the same length.
            If `valid_length` is a Symbol or NDArray, it should have shape (batch_size,).
            The ith element will be the length of the ith sequence in the batch.
            The last valid state will be return and the padded outputs will be masked with 0.
            Note that `valid_length` must be smaller or equal to `length`.

        Returns
        -------
        outputs : list of Symbol or Symbol
            Symbol (if `merge_outputs` is True) or list of Symbols
            (if `merge_outputs` is False) corresponding to the output from
            the RNN from this unrolling.

        states : list of Symbol
            The new state of this RNN after this unrolling.
            The type of this symbol is same as the output of `begin_state()`.
        """
        # pylint: disable=too-many-locals
        self.reset()

        inputs, axis, F, batch_size = _format_sequence(length, inputs, layout, False)
        begin_state = _get_begin_state(self, F, begin_state, inputs, batch_size)

        states = begin_state
        outputs = []
        all_states = []
        for i in range(length):
            output, states = self(inputs[i], states)
            outputs.append(output)
            if valid_length is not None:
                all_states.append(states)
        if valid_length is not None:
            states = [F.SequenceLast(F.stack(*ele_list, axis=0),
                                     sequence_length=valid_length,
                                     use_sequence_length=True,
                                     axis=0)
                      for ele_list in zip(*all_states)]
            outputs = _mask_sequence_variable_length(F, outputs, length, valid_length, axis, True)
        outputs, _, _, _ = _format_sequence(length, outputs, layout, merge_outputs)

        return outputs, states

    #pylint: disable=no-self-use
    def _get_activation(self, F, inputs, activation, **kwargs):
        """Get activation function. Convert if is string"""
        func = {'tanh': F.tanh,
                'relu': F.relu,
                'sigmoid': F.sigmoid,
                'softsign': F.softsign}.get(activation)
        if func:
            return func(inputs, **kwargs)
        elif isinstance(activation, string_types):
            return F.Activation(inputs, act_type=activation, **kwargs)
        elif isinstance(activation, LeakyReLU):
            return F.LeakyReLU(inputs, act_type='leaky', slope=activation._alpha, **kwargs)
        return activation(inputs, **kwargs)

    def forward(self, inputs, states):
        """Unrolls the recurrent cell for one time step.

        Parameters
        ----------
        inputs : sym.Variable
            Input symbol, 2D, of shape (batch_size * num_units).
        states : list of sym.Variable
            RNN state from previous step or the output of begin_state().

        Returns
        -------
        output : Symbol
            Symbol corresponding to the output from the RNN when unrolling
            for a single time step.
        states : list of Symbol
            The new state of this RNN after this unrolling.
            The type of this symbol is same as the output of `begin_state()`.
            This can be used as an input state to the next time step
            of this RNN.

        See Also
        --------
        begin_state: This function can provide the states for the first time step.
        unroll: This function unrolls an RNN for a given number of (>=1) time steps.
        """
        # pylint: disable= arguments-differ
        self._counter += 1
        return super(RecurrentCell, self).forward(inputs, states)


class HybridRecurrentCell(RecurrentCell, HybridBlock):
    """HybridRecurrentCell supports hybridize."""
    def __init__(self, prefix=None, params=None):
        super(HybridRecurrentCell, self).__init__(prefix=prefix, params=params)

    def hybrid_forward(self, F, x, *args, **kwargs):
        raise NotImplementedError


class RNNCell(HybridRecurrentCell):
    r"""Elman RNN recurrent neural network cell.

    Each call computes the following function:

    .. math::

        h_t = \tanh(w_{ih} * x_t + b_{ih}  +  w_{hh} * h_{(t-1)} + b_{hh})

    where :math:`h_t` is the hidden state at time `t`, and :math:`x_t` is the hidden
    state of the previous layer at time `t` or :math:`input_t` for the first layer.
    If nonlinearity='relu', then `ReLU` is used instead of `tanh`.

    Parameters
    ----------
    hidden_size : int
        Number of units in output symbol
    activation : str or Symbol, default 'tanh'
        Type of activation function.
    i2h_weight_initializer : str or Initializer
        Initializer for the input weights matrix, used for the linear
        transformation of the inputs.
    h2h_weight_initializer : str or Initializer
        Initializer for the recurrent weights matrix, used for the linear
        transformation of the recurrent state.
    i2h_bias_initializer : str or Initializer, default 'zeros'
        Initializer for the bias vector.
    h2h_bias_initializer : str or Initializer, default 'zeros'
        Initializer for the bias vector.
    prefix : str, default ``'rnn_'``
        Prefix for name of `Block`s
        (and name of weight if params is `None`).
    params : Parameter or None
        Container for weight sharing between cells.
        Created if `None`.


    Inputs:
        - **data**: input tensor with shape `(batch_size, input_size)`.
        - **states**: a list of one initial recurrent state tensor with shape
          `(batch_size, num_hidden)`.

    Outputs:
        - **out**: output tensor with shape `(batch_size, num_hidden)`.
        - **next_states**: a list of one output recurrent state tensor with the
          same shape as `states`.
    """
    def __init__(self, hidden_size, activation='tanh',
                 i2h_weight_initializer=None, h2h_weight_initializer=None,
                 i2h_bias_initializer='zeros', h2h_bias_initializer='zeros',
                 input_size=0, prefix=None, params=None):
        super(RNNCell, self).__init__(prefix=prefix, params=params)
        self._hidden_size = hidden_size
        self._activation = activation
        self._input_size = input_size
        self.i2h_weight = self.params.get('i2h_weight', shape=(hidden_size, input_size),
                                          init=i2h_weight_initializer,
                                          allow_deferred_init=True)
        self.h2h_weight = self.params.get('h2h_weight', shape=(hidden_size, hidden_size),
                                          init=h2h_weight_initializer,
                                          allow_deferred_init=True)
        self.i2h_bias = self.params.get('i2h_bias', shape=(hidden_size,),
                                        init=i2h_bias_initializer,
                                        allow_deferred_init=True)
        self.h2h_bias = self.params.get('h2h_bias', shape=(hidden_size,),
                                        init=h2h_bias_initializer,
                                        allow_deferred_init=True)

    def state_info(self, batch_size=0):
        return [{'shape': (batch_size, self._hidden_size), '__layout__': 'NC'}]

    def _alias(self):
        return 'rnn'

    def __repr__(self):
        s = '{name}({mapping}'
        if hasattr(self, '_activation'):
            s += ', {_activation}'
        s += ')'
        shape = self.i2h_weight.shape
        mapping = '{0} -> {1}'.format(shape[1] if shape[1] else None, shape[0])
        return s.format(name=self.__class__.__name__,
                        mapping=mapping,
                        **self.__dict__)

    def hybrid_forward(self, F, inputs, states, i2h_weight,
                       h2h_weight, i2h_bias, h2h_bias):
        prefix = 't%d_'%self._counter
        i2h = F.FullyConnected(data=inputs, weight=i2h_weight, bias=i2h_bias,
                               num_hidden=self._hidden_size,
                               name=prefix+'i2h')
        h2h = F.FullyConnected(data=states[0], weight=h2h_weight, bias=h2h_bias,
                               num_hidden=self._hidden_size,
                               name=prefix+'h2h')
        i2h_plus_h2h = F.elemwise_add(i2h, h2h, name=prefix+'plus0')
        output = self._get_activation(F, i2h_plus_h2h, self._activation,
                                      name=prefix+'out')

        return output, [output]


class LSTMCell(HybridRecurrentCell):
    r"""Long-Short Term Memory (LSTM) network cell.

    Each call computes the following function:

    .. math::
        \begin{array}{ll}
        i_t = sigmoid(W_{ii} x_t + b_{ii} + W_{hi} h_{(t-1)} + b_{hi}) \\
        f_t = sigmoid(W_{if} x_t + b_{if} + W_{hf} h_{(t-1)} + b_{hf}) \\
        g_t = \tanh(W_{ig} x_t + b_{ig} + W_{hc} h_{(t-1)} + b_{hg}) \\
        o_t = sigmoid(W_{io} x_t + b_{io} + W_{ho} h_{(t-1)} + b_{ho}) \\
        c_t = f_t * c_{(t-1)} + i_t * g_t \\
        h_t = o_t * \tanh(c_t)
        \end{array}

    where :math:`h_t` is the hidden state at time `t`, :math:`c_t` is the
    cell state at time `t`, :math:`x_t` is the hidden state of the previous
    layer at time `t` or :math:`input_t` for the first layer, and :math:`i_t`,
    :math:`f_t`, :math:`g_t`, :math:`o_t` are the input, forget, cell, and
    out gates, respectively.

    Parameters
    ----------
    hidden_size : int
        Number of units in output symbol.
    i2h_weight_initializer : str or Initializer
        Initializer for the input weights matrix, used for the linear
        transformation of the inputs.
    h2h_weight_initializer : str or Initializer
        Initializer for the recurrent weights matrix, used for the linear
        transformation of the recurrent state.
    i2h_bias_initializer : str or Initializer, default 'zeros'
        Initializer for the bias vector.
    h2h_bias_initializer : str or Initializer, default 'zeros'
        Initializer for the bias vector.
    prefix : str, default ``'lstm_'``
        Prefix for name of `Block`s
        (and name of weight if params is `None`).
    params : Parameter or None, default None
        Container for weight sharing between cells.
        Created if `None`.
    activation : str, default 'tanh'
        Activation type to use. See nd/symbol Activation
        for supported types.
    recurrent_activation : str, default 'sigmoid'
        Activation type to use for the recurrent step. See nd/symbol Activation
        for supported types.

    Inputs:
        - **data**: input tensor with shape `(batch_size, input_size)`.
        - **states**: a list of two initial recurrent state tensors. Each has shape
          `(batch_size, num_hidden)`.

    Outputs:
        - **out**: output tensor with shape `(batch_size, num_hidden)`.
        - **next_states**: a list of two output recurrent state tensors. Each has
          the same shape as `states`.
    """
    # pylint: disable=too-many-instance-attributes
    def __init__(self, hidden_size,
                 i2h_weight_initializer=None, h2h_weight_initializer=None,
                 i2h_bias_initializer='zeros', h2h_bias_initializer='zeros',
                 input_size=0, prefix=None, params=None, activation='tanh',
                 recurrent_activation='sigmoid'):
        super(LSTMCell, self).__init__(prefix=prefix, params=params)

        self._hidden_size = hidden_size
        self._input_size = input_size
        self.i2h_weight = self.params.get('i2h_weight', shape=(4*hidden_size, input_size),
                                          init=i2h_weight_initializer,
                                          allow_deferred_init=True)
        self.h2h_weight = self.params.get('h2h_weight', shape=(4*hidden_size, hidden_size),
                                          init=h2h_weight_initializer,
                                          allow_deferred_init=True)
        self.i2h_bias = self.params.get('i2h_bias', shape=(4*hidden_size,),
                                        init=i2h_bias_initializer,
                                        allow_deferred_init=True)
        self.h2h_bias = self.params.get('h2h_bias', shape=(4*hidden_size,),
                                        init=h2h_bias_initializer,
                                        allow_deferred_init=True)
        self._activation = activation
        self._recurrent_activation = recurrent_activation


    def state_info(self, batch_size=0):
        return [{'shape': (batch_size, self._hidden_size), '__layout__': 'NC'},
                {'shape': (batch_size, self._hidden_size), '__layout__': 'NC'}]

    def _alias(self):
        return 'lstm'

    def __repr__(self):
        s = '{name}({mapping})'
        shape = self.i2h_weight.shape
        mapping = '{0} -> {1}'.format(shape[1] if shape[1] else None, shape[0])
        return s.format(name=self.__class__.__name__,
                        mapping=mapping,
                        **self.__dict__)

    def hybrid_forward(self, F, inputs, states, i2h_weight,
                       h2h_weight, i2h_bias, h2h_bias):
        # pylint: disable=too-many-locals
        prefix = 't%d_'%self._counter
        i2h = F.FullyConnected(data=inputs, weight=i2h_weight, bias=i2h_bias,
                               num_hidden=self._hidden_size*4, name=prefix+'i2h')
        h2h = F.FullyConnected(data=states[0], weight=h2h_weight, bias=h2h_bias,
                               num_hidden=self._hidden_size*4, name=prefix+'h2h')
        gates = F.elemwise_add(i2h, h2h, name=prefix+'plus0')
        slice_gates = F.SliceChannel(gates, num_outputs=4, name=prefix+'slice')
        in_gate = self._get_activation(
            F, slice_gates[0], self._recurrent_activation, name=prefix+'i')
        forget_gate = self._get_activation(
            F, slice_gates[1], self._recurrent_activation, name=prefix+'f')
        in_transform = self._get_activation(
            F, slice_gates[2], self._activation, name=prefix+'c')
        out_gate = self._get_activation(
            F, slice_gates[3], self._recurrent_activation, name=prefix+'o')
        next_c = F._internal._plus(F.elemwise_mul(forget_gate, states[1], name=prefix+'mul0'),
                                   F.elemwise_mul(in_gate, in_transform, name=prefix+'mul1'),
                                   name=prefix+'state')
        next_h = F._internal._mul(out_gate, F.Activation(next_c, act_type=self._activation, name=prefix+'activation0'),
                                  name=prefix+'out')

        return next_h, [next_h, next_c]


class GRUCell(HybridRecurrentCell):
    r"""Gated Rectified Unit (GRU) network cell.
    Note: this is an implementation of the cuDNN version of GRUs
    (slight modification compared to Cho et al. 2014; the reset gate :math:`r_t`
    is applied after matrix multiplication).

    Each call computes the following function:

    .. math::
        \begin{array}{ll}
        r_t = sigmoid(W_{ir} x_t + b_{ir} + W_{hr} h_{(t-1)} + b_{hr}) \\
        i_t = sigmoid(W_{ii} x_t + b_{ii} + W_{hi} h_{(t-1)} + b_{hi}) \\
        n_t = \tanh(W_{in} x_t + b_{in} + r_t * (W_{hn} h_{(t-1)} + b_{hn})) \\
        h_t = (1 - i_t) * n_t + i_t * h_{(t-1)} \\
        \end{array}

    where :math:`h_t` is the hidden state at time `t`, :math:`x_t` is the hidden
    state of the previous layer at time `t` or :math:`input_t` for the first layer,
    and :math:`r_t`, :math:`i_t`, :math:`n_t` are the reset, input, and new gates, respectively.

    Parameters
    ----------
    hidden_size : int
        Number of units in output symbol.
    i2h_weight_initializer : str or Initializer
        Initializer for the input weights matrix, used for the linear
        transformation of the inputs.
    h2h_weight_initializer : str or Initializer
        Initializer for the recurrent weights matrix, used for the linear
        transformation of the recurrent state.
    i2h_bias_initializer : str or Initializer, default 'zeros'
        Initializer for the bias vector.
    h2h_bias_initializer : str or Initializer, default 'zeros'
        Initializer for the bias vector.
    prefix : str, default ``'gru_'``
        prefix for name of `Block`s
        (and name of weight if params is `None`).
    params : Parameter or None, default None
        Container for weight sharing between cells.
        Created if `None`.


    Inputs:
        - **data**: input tensor with shape `(batch_size, input_size)`.
        - **states**: a list of one initial recurrent state tensor with shape
          `(batch_size, num_hidden)`.

    Outputs:
        - **out**: output tensor with shape `(batch_size, num_hidden)`.
        - **next_states**: a list of one output recurrent state tensor with the
          same shape as `states`.
    """
    def __init__(self, hidden_size,
                 i2h_weight_initializer=None, h2h_weight_initializer=None,
                 i2h_bias_initializer='zeros', h2h_bias_initializer='zeros',
                 input_size=0, prefix=None, params=None):
        super(GRUCell, self).__init__(prefix=prefix, params=params)
        self._hidden_size = hidden_size
        self._input_size = input_size
        self.i2h_weight = self.params.get('i2h_weight', shape=(3*hidden_size, input_size),
                                          init=i2h_weight_initializer,
                                          allow_deferred_init=True)
        self.h2h_weight = self.params.get('h2h_weight', shape=(3*hidden_size, hidden_size),
                                          init=h2h_weight_initializer,
                                          allow_deferred_init=True)
        self.i2h_bias = self.params.get('i2h_bias', shape=(3*hidden_size,),
                                        init=i2h_bias_initializer,
                                        allow_deferred_init=True)
        self.h2h_bias = self.params.get('h2h_bias', shape=(3*hidden_size,),
                                        init=h2h_bias_initializer,
                                        allow_deferred_init=True)

    def state_info(self, batch_size=0):
        return [{'shape': (batch_size, self._hidden_size), '__layout__': 'NC'}]

    def _alias(self):
        return 'gru'

    def __repr__(self):
        s = '{name}({mapping})'
        shape = self.i2h_weight.shape
        mapping = '{0} -> {1}'.format(shape[1] if shape[1] else None, shape[0])
        return s.format(name=self.__class__.__name__,
                        mapping=mapping,
                        **self.__dict__)

    def hybrid_forward(self, F, inputs, states, i2h_weight,
                       h2h_weight, i2h_bias, h2h_bias):
        # pylint: disable=too-many-locals
        prefix = 't%d_'%self._counter
        prev_state_h = states[0]
        i2h = F.FullyConnected(data=inputs,
                               weight=i2h_weight,
                               bias=i2h_bias,
                               num_hidden=self._hidden_size * 3,
                               name=prefix+'i2h')
        h2h = F.FullyConnected(data=prev_state_h,
                               weight=h2h_weight,
                               bias=h2h_bias,
                               num_hidden=self._hidden_size * 3,
                               name=prefix+'h2h')

        i2h_r, i2h_z, i2h = F.SliceChannel(i2h, num_outputs=3,
                                           name=prefix+'i2h_slice')
        h2h_r, h2h_z, h2h = F.SliceChannel(h2h, num_outputs=3,
                                           name=prefix+'h2h_slice')

        reset_gate = F.Activation(F.elemwise_add(i2h_r, h2h_r, name=prefix+'plus0'), act_type="sigmoid",
                                  name=prefix+'r_act')
        update_gate = F.Activation(F.elemwise_add(i2h_z, h2h_z, name=prefix+'plus1'), act_type="sigmoid",
                                   name=prefix+'z_act')

        next_h_tmp = F.Activation(F.elemwise_add(i2h,
                                                 F.elemwise_mul(reset_gate, h2h, name=prefix+'mul0'),
                                                 name=prefix+'plus2'),
                                  act_type="tanh",
                                  name=prefix+'h_act')

        ones = F.ones_like(update_gate, name=prefix+"ones_like0")
        next_h = F._internal._plus(F.elemwise_mul(F.elemwise_sub(ones, update_gate, name=prefix+'minus0'),
                                                  next_h_tmp,
                                                  name=prefix+'mul1'),
                                   F.elemwise_mul(update_gate, prev_state_h, name=prefix+'mul20'),
                                   name=prefix+'out')

        return next_h, [next_h]


class SequentialRNNCell(RecurrentCell):
    """Sequentially stacking multiple RNN cells."""
    def __init__(self, prefix=None, params=None):
        super(SequentialRNNCell, self).__init__(prefix=prefix, params=params)

    def __repr__(self):
        s = '{name}(\n{modstr}\n)'
        return s.format(name=self.__class__.__name__,
                        modstr='\n'.join(['({i}): {m}'.format(i=i, m=_indent(m.__repr__(), 2))
                                          for i, m in self._children.items()]))

    def add(self, cell):
        """Appends a cell into the stack.

        Parameters
        ----------
        cell : RecurrentCell
            The cell to add.
        """
        self.register_child(cell)

    def state_info(self, batch_size=0):
        return _cells_state_info(self._children.values(), batch_size)

    def begin_state(self, **kwargs):
        assert not self._modified, \
            "After applying modifier cells (e.g. ZoneoutCell) the base " \
            "cell cannot be called directly. Call the modifier cell instead."
        return _cells_begin_state(self._children.values(), **kwargs)

    def __call__(self, inputs, states):
        self._counter += 1
        next_states = []
        p = 0
        assert all(not isinstance(cell, BidirectionalCell) for cell in self._children.values())
        for cell in self._children.values():
            assert not isinstance(cell, BidirectionalCell)
            n = len(cell.state_info())
            state = states[p:p+n]
            p += n
            inputs, state = cell(inputs, state)
            next_states.append(state)
        return inputs, sum(next_states, [])

    def unroll(self, length, inputs, begin_state=None, layout='NTC', merge_outputs=None,
               valid_length=None):
        # pylint: disable=too-many-locals
        self.reset()

        inputs, _, F, batch_size = _format_sequence(length, inputs, layout, None)
        num_cells = len(self._children)
        begin_state = _get_begin_state(self, F, begin_state, inputs, batch_size)

        p = 0
        next_states = []
        for i, cell in enumerate(self._children.values()):
            n = len(cell.state_info())
            states = begin_state[p:p+n]
            p += n
            inputs, states = cell.unroll(length, inputs=inputs, begin_state=states,
                                         layout=layout,
                                         merge_outputs=None if i < num_cells-1 else merge_outputs,
                                         valid_length=valid_length)
            next_states.extend(states)

        return inputs, next_states

    def __getitem__(self, i):
        return self._children[str(i)]

    def __len__(self):
        return len(self._children)

    def hybrid_forward(self, *args, **kwargs):
        # pylint: disable=missing-docstring
        raise NotImplementedError


class HybridSequentialRNNCell(HybridRecurrentCell):
    """Sequentially stacking multiple HybridRNN cells."""
    def __init__(self, prefix=None, params=None):
        super(HybridSequentialRNNCell, self).__init__(prefix=prefix, params=params)

    def __repr__(self):
        s = '{name}(\n{modstr}\n)'
        return s.format(name=self.__class__.__name__,
                        modstr='\n'.join(['({i}): {m}'.format(i=i, m=_indent(m.__repr__(), 2))
                                          for i, m in self._children.items()]))

    def add(self, cell):
        """Appends a cell into the stack.

        Parameters
        ----------
        cell : RecurrentCell
            The cell to add.
        """
        self.register_child(cell)

    def state_info(self, batch_size=0):
        return _cells_state_info(self._children.values(), batch_size)

    def begin_state(self, **kwargs):
        assert not self._modified, \
            "After applying modifier cells (e.g. ZoneoutCell) the base " \
            "cell cannot be called directly. Call the modifier cell instead."
        return _cells_begin_state(self._children.values(), **kwargs)

    def __call__(self, inputs, states):
        self._counter += 1
        next_states = []
        p = 0
        assert all(not isinstance(cell, BidirectionalCell) for cell in self._children.values())
        for cell in self._children.values():
            n = len(cell.state_info())
            state = states[p:p+n]
            p += n
            inputs, state = cell(inputs, state)
            next_states.append(state)
        return inputs, sum(next_states, [])

    def unroll(self, length, inputs, begin_state=None, layout='NTC', merge_outputs=None,
               valid_length=None):
        self.reset()

        inputs, _, F, batch_size = _format_sequence(length, inputs, layout, None)
        num_cells = len(self._children)
        begin_state = _get_begin_state(self, F, begin_state, inputs, batch_size)

        p = 0
        next_states = []
        for i, cell in enumerate(self._children.values()):
            n = len(cell.state_info())
            states = begin_state[p:p+n]
            p += n
            inputs, states = cell.unroll(length, inputs=inputs, begin_state=states,
                                         layout=layout,
                                         merge_outputs=None if i < num_cells-1 else merge_outputs,
                                         valid_length=valid_length)
            next_states.extend(states)

        return inputs, next_states

    def __getitem__(self, i):
        return self._children[str(i)]

    def __len__(self):
        return len(self._children)

    def hybrid_forward(self, F, inputs, states):
        return self.__call__(inputs, states)


class DropoutCell(HybridRecurrentCell):
    """Applies dropout on input.

    Parameters
    ----------
    rate : float
        Percentage of elements to drop out, which
        is 1 - percentage to retain.
    axes : tuple of int, default ()
        The axes on which dropout mask is shared. If empty, regular dropout is applied.


    Inputs:
        - **data**: input tensor with shape `(batch_size, size)`.
        - **states**: a list of recurrent state tensors.

    Outputs:
        - **out**: output tensor with shape `(batch_size, size)`.
        - **next_states**: returns input `states` directly.
    """
    def __init__(self, rate, axes=(), prefix=None, params=None):
        super(DropoutCell, self).__init__(prefix, params)
        assert isinstance(rate, numeric_types), "rate must be a number"
        self._rate = rate
        self._axes = axes

    def __repr__(self):
        s = '{name}(rate={_rate}, axes={_axes})'
        return s.format(name=self.__class__.__name__,
                        **self.__dict__)

    def state_info(self, batch_size=0):
        return []

    def _alias(self):
        return 'dropout'

    def hybrid_forward(self, F, inputs, states):
        if self._rate > 0:
            inputs = F.Dropout(data=inputs, p=self._rate, axes=self._axes,
                               name='t%d_fwd'%self._counter)
        return inputs, states

    def unroll(self, length, inputs, begin_state=None, layout='NTC', merge_outputs=None,
               valid_length=None):
        self.reset()

        inputs, _, F, _ = _format_sequence(length, inputs, layout, merge_outputs)
        if isinstance(inputs, tensor_types):
            return self.hybrid_forward(F, inputs, begin_state if begin_state else [])
        return super(DropoutCell, self).unroll(
            length, inputs, begin_state=begin_state, layout=layout,
            merge_outputs=merge_outputs, valid_length=None)


class ModifierCell(HybridRecurrentCell):
    """Base class for modifier cells. A modifier
    cell takes a base cell, apply modifications
    on it (e.g. Zoneout), and returns a new cell.

    After applying modifiers the base cell should
    no longer be called directly. The modifier cell
    should be used instead.
    """
    def __init__(self, base_cell):
        assert not base_cell._modified, \
            "Cell %s is already modified. One cell cannot be modified twice"%base_cell.name
        base_cell._modified = True
        super(ModifierCell, self).__init__(prefix=base_cell.prefix+self._alias(),
                                           params=None)
        self.base_cell = base_cell

    @property
    def params(self):
        return self.base_cell.params

    def state_info(self, batch_size=0):
        return self.base_cell.state_info(batch_size)

    def begin_state(self, func=symbol.zeros, **kwargs):
        assert not self._modified, \
            "After applying modifier cells (e.g. DropoutCell) the base " \
            "cell cannot be called directly. Call the modifier cell instead."
        self.base_cell._modified = False
        begin = self.base_cell.begin_state(func=func, **kwargs)
        self.base_cell._modified = True
        return begin

    def hybrid_forward(self, F, inputs, states):
        raise NotImplementedError

    def __repr__(self):
        s = '{name}({base_cell})'
        return s.format(name=self.__class__.__name__,
                        **self.__dict__)


class ZoneoutCell(ModifierCell):
    """Applies Zoneout on base cell."""
    def __init__(self, base_cell, zoneout_outputs=0., zoneout_states=0.):
        assert not isinstance(base_cell, BidirectionalCell), \
            "BidirectionalCell doesn't support zoneout since it doesn't support step. " \
            "Please add ZoneoutCell to the cells underneath instead."
        assert not isinstance(base_cell, SequentialRNNCell) or not base_cell._bidirectional, \
            "Bidirectional SequentialRNNCell doesn't support zoneout. " \
            "Please add ZoneoutCell to the cells underneath instead."
        super(ZoneoutCell, self).__init__(base_cell)
        self.zoneout_outputs = zoneout_outputs
        self.zoneout_states = zoneout_states
        self._prev_output = None

    def __repr__(self):
        s = '{name}(p_out={zoneout_outputs}, p_state={zoneout_states}, {base_cell})'
        return s.format(name=self.__class__.__name__,
                        **self.__dict__)

    def _alias(self):
        return 'zoneout'

    def reset(self):
        super(ZoneoutCell, self).reset()
        self._prev_output = None

    def hybrid_forward(self, F, inputs, states):
        cell, p_outputs, p_states = self.base_cell, self.zoneout_outputs, self.zoneout_states
        next_output, next_states = cell(inputs, states)
        mask = (lambda p, like: F.Dropout(F.ones_like(like), p=p))

        prev_output = self._prev_output
        if prev_output is None:
            prev_output = F.zeros_like(next_output)

        output = (F.where(mask(p_outputs, next_output), next_output, prev_output)
                  if p_outputs != 0. else next_output)
        states = ([F.where(mask(p_states, new_s), new_s, old_s) for new_s, old_s in
                   zip(next_states, states)] if p_states != 0. else next_states)

        self._prev_output = output

        return output, states


class ResidualCell(ModifierCell):
    """
    Adds residual connection as described in Wu et al, 2016
    (https://arxiv.org/abs/1609.08144).
    Output of the cell is output of the base cell plus input.
    """

    def __init__(self, base_cell):
        # pylint: disable=useless-super-delegation
        super(ResidualCell, self).__init__(base_cell)

    def hybrid_forward(self, F, inputs, states):
        output, states = self.base_cell(inputs, states)
        output = F.elemwise_add(output, inputs, name='t%d_fwd'%self._counter)
        return output, states

    def unroll(self, length, inputs, begin_state=None, layout='NTC', merge_outputs=None,
               valid_length=None):
        self.reset()

        self.base_cell._modified = False
        outputs, states = self.base_cell.unroll(length, inputs=inputs, begin_state=begin_state,
                                                layout=layout, merge_outputs=merge_outputs,
                                                valid_length=valid_length)
        self.base_cell._modified = True

        merge_outputs = isinstance(outputs, tensor_types) if merge_outputs is None else \
                        merge_outputs
        inputs, axis, F, _ = _format_sequence(length, inputs, layout, merge_outputs)
        if valid_length is not None:
            # mask the padded inputs to zero
            inputs = _mask_sequence_variable_length(F, inputs, length, valid_length, axis,
                                                    merge_outputs)
        if merge_outputs:
            outputs = F.elemwise_add(outputs, inputs)
        else:
            outputs = [F.elemwise_add(i, j) for i, j in zip(outputs, inputs)]

        return outputs, states


class BidirectionalCell(HybridRecurrentCell):
    """Bidirectional RNN cell.

    Parameters
    ----------
    l_cell : RecurrentCell
        Cell for forward unrolling
    r_cell : RecurrentCell
        Cell for backward unrolling
    """
    def __init__(self, l_cell, r_cell, output_prefix='bi_'):
        super(BidirectionalCell, self).__init__(prefix='', params=None)
        self.register_child(l_cell, 'l_cell')
        self.register_child(r_cell, 'r_cell')
        self._output_prefix = output_prefix

    def __call__(self, inputs, states):
        raise NotImplementedError("Bidirectional cannot be stepped. Please use unroll")

    def __repr__(self):
        s = '{name}(forward={l_cell}, backward={r_cell})'
        return s.format(name=self.__class__.__name__,
                        l_cell=self._children['l_cell'],
                        r_cell=self._children['r_cell'])

    def state_info(self, batch_size=0):
        return _cells_state_info(self._children.values(), batch_size)

    def begin_state(self, **kwargs):
        assert not self._modified, \
            "After applying modifier cells (e.g. DropoutCell) the base " \
            "cell cannot be called directly. Call the modifier cell instead."
        return _cells_begin_state(self._children.values(), **kwargs)

    def unroll(self, length, inputs, begin_state=None, layout='NTC', merge_outputs=None,
               valid_length=None):
        # pylint: disable=too-many-locals
        self.reset()

        inputs, axis, F, batch_size = _format_sequence(length, inputs, layout, False)
        if valid_length is None:
            reversed_inputs = list(reversed(inputs))
        else:
            reversed_inputs = F.SequenceReverse(F.stack(*inputs, axis=0),
                                                sequence_length=valid_length,
                                                use_sequence_length=True)
            reversed_inputs = _as_list(F.split(reversed_inputs, axis=0, num_outputs=length,
                                               squeeze_axis=True))
        begin_state = _get_begin_state(self, F, begin_state, inputs, batch_size)

        states = begin_state
        l_cell, r_cell = self._children.values()
        l_outputs, l_states = l_cell.unroll(length, inputs=inputs,
                                            begin_state=states[:len(l_cell.state_info(batch_size))],
                                            layout=layout, merge_outputs=merge_outputs,
                                            valid_length=valid_length)
        r_outputs, r_states = r_cell.unroll(length,
                                            inputs=reversed_inputs,
                                            begin_state=states[len(l_cell.state_info(batch_size)):],
                                            layout=layout, merge_outputs=False,
                                            valid_length=valid_length)
        if valid_length is None:
            reversed_r_outputs = list(reversed(r_outputs))
        else:
            reversed_r_outputs = F.SequenceReverse(F.stack(*r_outputs, axis=0),
                                                   sequence_length=valid_length,
                                                   use_sequence_length=True,
                                                   axis=0)
            reversed_r_outputs = _as_list(F.split(reversed_r_outputs, axis=0, num_outputs=length,
                                                  squeeze_axis=True))
        if merge_outputs is None:
            merge_outputs = isinstance(l_outputs, tensor_types)
            l_outputs, _, _, _ = _format_sequence(None, l_outputs, layout, merge_outputs)
            reversed_r_outputs, _, _, _ = _format_sequence(None, reversed_r_outputs, layout,
                                                           merge_outputs)

        if merge_outputs:
            reversed_r_outputs = F.stack(*reversed_r_outputs, axis=axis)
            outputs = F.concat(l_outputs, reversed_r_outputs, dim=2,
                               name='%sout'%self._output_prefix)

        else:
            outputs = [F.concat(l_o, r_o, dim=1, name='%st%d'%(self._output_prefix, i))
                       for i, (l_o, r_o) in enumerate(zip(l_outputs, reversed_r_outputs))]
        if valid_length is not None:
            outputs = _mask_sequence_variable_length(F, outputs, length, valid_length, axis,
                                                     merge_outputs)
        states = l_states + r_states
        return outputs, states
