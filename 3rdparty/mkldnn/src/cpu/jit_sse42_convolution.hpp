/*******************************************************************************
* Copyright 2017-2018 Intel Corporation
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/

#ifndef CPU_JIT_SSE42_CONVOLUTION_HPP
#define CPU_JIT_SSE42_CONVOLUTION_HPP

#include "c_types_map.hpp"
#include "cpu_convolution_pd.hpp"
#include "cpu_engine.hpp"
#include "jit_primitive_conf.hpp"
#include "jit_sse42_conv_kernel_f32.hpp"

namespace mkldnn {
namespace impl {
namespace cpu {

template <bool with_relu>
struct _jit_sse42_convolution_fwd_t: public cpu_primitive_t {
    struct pd_t: public _cpu_convolution_fwd_pd_t<with_relu> {
        pd_t(engine_t *engine,
                const typename pd_t::base_desc_t *adesc,
                const primitive_attr_t *attr,
                const typename pd_t::base_class *hint_fwd_pd)
            : _cpu_convolution_fwd_pd_t<with_relu>(engine, adesc, attr,
                    hint_fwd_pd)
            , jcp_() {}

        DECLARE_COMMON_PD_T(
                JIT_IMPL_NAME_HELPER("jit:", sse42, ""),
                _jit_sse42_convolution_fwd_t<with_relu>);

        virtual status_t init() override {
            using namespace prop_kind;
            assert(this->engine()->kind() == engine_kind::cpu);
            bool ok = true
                && this->set_default_params() == status::success
                && utils::one_of(this->cdesc_().prop_kind, forward_training,
                        forward_inference)
                && this->cdesc_().alg_kind == alg_kind::convolution_direct
                && !this->has_zero_dim_memory()
                && utils::everyone_is(data_type::f32,
                        this->cdesc_().src_desc.data_type,
                        this->cdesc_().weights_desc.data_type,
                        this->cdesc_().dst_desc.data_type)
                && IMPLICATION(this->with_bias(),
                        data_type::f32 == this->cdesc_().bias_desc.data_type);
            if (!ok) return status::unimplemented;

            return jit_sse42_conv_fwd_kernel_f32::init_conf(jcp_, this->cdesc_(),
                    *this->src_pd_.desc(), *this->weights_pd_.desc(),
                    *this->dst_pd_.desc(), *this->attr(), with_relu,
                    this->negative_slope());
        }

        jit_conv_conf_t jcp_;

    protected:
        virtual status_t set_default_params() override {
            using namespace memory_format;

            const bool flat = this->IC() == 3;
            if (this->src_pd_.desc()->format == any)
                CHECK(this->src_pd_.set_format(flat
                    ? utils::pick(this->ndims() - 3, ncw, nchw)
                    : utils::pick(this->ndims() - 3, nCw8c, nChw8c)));
            if (this->dst_pd_.desc()->format == any)
                CHECK(this->dst_pd_.set_format(utils::pick(this->ndims() - 3,
                    nCw8c, nChw8c)));
            if (this->weights_pd_.desc()->format == any)
                CHECK(this->weights_pd_.set_format(this->with_groups()
                    ? utils::pick(2 * this->ndims() - 6 + flat, gOIw8i8o,
                        gOwi8o, gOIhw8i8o, gOhwi8o)
                    : utils::pick(2 * this->ndims() - 6 + flat, OIw8i8o, Owi8o,
                        OIhw8i8o, Ohwi8o)));
            if (this->bias_pd_.desc()->format == any)
                CHECK(this->bias_pd_.set_format(x));
            return status::success;
        }
    };

    _jit_sse42_convolution_fwd_t(const pd_t *pd, const input_vector &inputs,
            const output_vector &outputs)
        : cpu_primitive_t(&conf_, inputs, outputs), conf_(*pd)
    { kernel_ = new jit_sse42_conv_fwd_kernel_f32(conf_.jcp_, *conf_.attr()); }
    ~_jit_sse42_convolution_fwd_t() { delete kernel_; };

    typedef typename prec_traits<data_type::f32>::type data_t;

    virtual void execute(event_t *e) {
        execute_forward();
        e->set_state(event_t::ready);
    }

private:
    void execute_forward();
    pd_t conf_;
    jit_sse42_conv_fwd_kernel_f32 *kernel_;
};

using jit_sse42_convolution_fwd_t = _jit_sse42_convolution_fwd_t<false>;
using jit_sse42_convolution_relu_t = _jit_sse42_convolution_fwd_t<true>;

}
}
}

#endif
