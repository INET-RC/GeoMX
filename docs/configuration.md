# Configuration Guide
This guidance describes the environmental variables and hyperparameters needed to launch each optimization technique in our GeoMX system.

## Synchronization Algorithms
GeoMX currently supports two fundamental synchronization algorithms, i.e., the fully-synchronous algorithm, the mixed-synchronous algorithm, and an advanced algorithm, i.e., hierarchical frequency aggregation.

### Fully Synchronous Algorithm
Fully Synchronous Algorithm (FSA) is the default strategy for model synchronization. In this synchronous algorithm, training nodes synchronize their model data (can be parameters or gradients) each round, and both parameter server systems within and between data centers run in a synchronous parallel mode. All training nodes are synchronized to ensure a consistent model. However, this comes at the expense of training speed, as it requires waiting for all computations and communications to complete at every iteration.

To use FSA, all that's required is to set `dist_sync` as a hyperparameter during the initialization of `kvstore`. For example:

```python
import mxnet as mx

# Initialize distributed kvstore in synchronous mode.
kvstore_dist = mx.kv.create("dist_sync")

# Master worker sets the optimizer of the global parameter server to Adam.
if kvstore_dist.is_master_worker:
    kvstore_dist.set_optimizer(mx.optimizer.Adam(learning_rate=lr))

for epoch in range(num_epochs):
    for _, batch in enumerate(train_iter):
        # Perform forward and backward propagation to calculate gradients.
        ...
        # Synchronize gradients to obtain updated parameters.
        for idx, param in enumerate(net_params):
            if param.grad_req == "null": continue
            kvstore_dist.push(idx, param.grad(), priority=-idx)
            kvstore_dist.pull(idx, param.data(), priority=-idx)
```

> The code can be found in `examples/`

### Mixed-synchronous Algorithm

Likewise, create a `dist_async` KVStore.

```python
import mxnet as mx

kvstore_dist = mx.kv.create("dist_async")
```

### HierFAVG Synchronous Algorithm

HierFAVG-styled Synchronous Algorithm is generally identical with Fully-synchronous Algorithm while the former adds a few loops before an local iteration or a global iteration.

```python
import mxnet as mx

kvstore_dist = mx.kv.create("dist_sync")
``` 

Some Environment Variables should be additionally set.

```shell
MXNET_KVSTORE_USE_HFA=0 # whether HierFAVG is enabled
MXNET_KVSTORE_HFA_K1=20 # loops before a local iteration
MXNET_KVSTORE_HFA_K2=10 # loops before a global iteration
```

### DC-ASGD Asynchronous Algorithm

To mitigate straggler problems brought by the asynchronous pattern, DCASGD optimizer is introduced.

```python
import mxnet as mx

kvstore_dist = mx.kv.create("dist_async")
kvstore_dist.set_optimizer(mx.optimizer.DCASGD(learning_rate=learning_rate))
```

## Communication-efficient Strategies

> Caution: Strategies mentioned below are only compatible with `Fully-synchronous Algorithm`

### BSC

In `.py` files, define the correct gradient compression type and an appropriate compression ratio.

```python
kvstore_dist.set_gradient_compression({"type": "bsc", "threshold": ratio})
```

Remember to set the compression lower bound according to your deep neural networks as a Environment Variable.

```shell
MXNET_KVSTORE_SIZE_LOWER_BOUND=200000
```

### DGT

Only a few Environment Variables are needed here.

```shell
ENABLE_DGT=0 # whether to enable DGT. Attention, 2 is for enabled, not 1
DMLC_UDP_CHANNEL_NUM=3 # Channels
DMLC_K=0.8 # compression ratio
ADAPTIVE_K_FLAG=1 # set K adaptively
```

### TSEngine

Only a few Environment Variables are needed here.

```shell
ENABLE_INTER_TS=0 # whether to enable locally
ENABLE_INTRA_TS=0 # whether to enable globally
MAX_GREED_RATE_TS=0.9
```

### P3

Only an Environment Variables are needed here.

```shell
ENABLE_P3=0 # whether to enable P3
```
