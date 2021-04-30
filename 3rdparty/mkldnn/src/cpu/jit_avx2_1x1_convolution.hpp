/*******************************************************************************
* Copyright 2016-2018 Intel Corporation
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

#ifndef CPU_JIT_AVX2_1x1_CONVOLUTION_HPP
#define CPU_JIT_AVX2_1x1_CONVOLUTION_HPP

#include "c_types_map.hpp"
#include "cpu_convolution_pd.hpp"
#include "cpu_engine.hpp"
#include "cpu_reducer.hpp"
#include "jit_avx2_1x1_conv_kernel_f32.hpp"
#include "jit_uni_1x1_conv_utils.hpp"
#include "mkldnn_thread.hpp"
#include "utils.hpp"

namespace mkldnn {
namespace impl {
namespace cpu {

template <bool with_relu>
struct _jit_avx2_1x1_convolution_fwd_t: public cpu_primitive_t {
    // TODO: (Roma) Code duplication duplication! Remove with templates
    //              (maybe...)!
    struct pd_t: public _cpu_convolution_fwd_pd_t<with_relu> {
        pd_t(engine_t *engine,
                const typename pd_t::base_desc_t *adesc,
                const primitive_attr_t *attr,
                const typename pd_t::base_class *hint_fwd_pd)
            : _cpu_convolution_fwd_pd_t<with_relu>(engine, adesc, attr,
                    hint_fwd_pd)
            , jcp_(), rtus_() {}

        DECLARE_COMMON_PD_T(
                JIT_IMPL_NAME_HELPER("jit_1x1:", avx2, ""),
                _jit_avx2_1x1_convolution_fwd_t<with_relu>);

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

            const convolution_desc_t *conv_d = &this->cdesc_();
            const memory_desc_t *src_d = this->src_pd_.desc();
            rtus_prepare(this, conv_d, src_d, this->dst_pd_.desc());

            return jit_avx2_1x1_conv_kernel_f32::init_conf(jcp_,
                    *conv_d, *src_d, *this->weights_pd_.desc(),
                    *this->dst_pd_.desc(), *this->attr(),
                    with_relu, this->negative_slope());
        }

        jit_1x1_conv_conf_t jcp_;
        struct reduce_to_unit_stride_t {
            convolution_desc_t conv_d_;
            bool reduce_src_;
        } rtus_;

    protected:
        virtual status_t set_default_params() override {
            using namespace memory_format;
            if (this->src_pd_.desc()->format == any)
                CHECK(this->src_pd_.set_format(utils::pick(this->ndims() - 3,
                    nCw8c, nChw8c)));
            if (this->dst_pd_.desc()->format == any)
                CHECK(this->dst_pd_.set_format(utils::pick(this->ndims() - 3,
                    nCw8c, nChw8c)));
            if (this->weights_pd_.desc()->format == any)
                CHECK(this->weights_pd_.set_format(this->with_groups()
                    ? utils::pick(this->ndims() - 3, gOIw8i8o, gOIhw8i8o)
                    : utils::pick(this->ndims() - 3, OIw8i8o, OIhw8i8o)));
            if (this->bias_pd_.desc()->format == any)
                CHECK(this->bias_pd_.set_format(x));
            return status::success;
        }
    };

    template <cpu_isa_t isa, typename conv_t>
    friend void init_rtus_driver(conv_t *self);

    _jit_avx2_1x1_convolution_fwd_t(const pd_t *pd, const input_vector &inputs,
            const output_vector &outputs)
        : cpu_primitive_t(&conf_, inputs, outputs), conf_(*pd)
        , kernel_(nullptr), rtus_driver_(nullptr), ws_per_thread_(0)
        , scratch_(nullptr), padded_bias_(nullptr)
    {
        kernel_ = new jit_avx2_1x1_conv_kernel_f32(conf_.jcp_, *conf_.attr());
        init_rtus_driver<avx2>(this);

        if (conf_.want_padded_bias()) {
            const auto &j = conf_.jcp_;
            assert(j.ngroups == 1);
            padded_bias_ = (data_t *)malloc(sizeof(data_t) * j.oc, 64);
            for (int oc = j.oc_without_padding; oc < j.oc; ++oc)
                padded_bias_[oc] = 0;
        }

    }
    ~_jit_avx2_1x1_convolution_fwd_t() {
        delete kernel_;
        delete rtus_driver_;
        free(scratch_);
        free(padded_bias_);
    }

    typedef typename prec_traits<data_type::f32>::type data_t;

    virtual void execute(event_t *e) {
        execute_forward();
        e->set_state(event_t::ready);
    }

private:
    void execute_forward();
    pd_t conf_;
    jit_avx2_1x1_conv_kernel_f32 *kernel_;

    /* reduction to unit stride */
    rtus_driver_t<avx2> *rtus_driver_;
    size_t ws_per_thread_;
    data_t *scratch_;
    data_t *padded_bias_;
};

using jit_avx2_1x1_convolution_fwd_t = _jit_avx2_1x1_convolution_fwd_t<false>;
using jit_avx2_1x1_convolution_relu_t = _jit_avx2_1x1_convolution_fwd_t<true>;

struct jit_avx2_1x1_convolution_bwd_data_t: public cpu_primitive_t {
    struct pd_t: public cpu_convolution_bwd_data_pd_t {
        pd_t(engine_t *engine,
                const convolution_desc_t *adesc,
                const primitive_attr_t *attr,
                const convolution_fwd_pd_t *hint_fwd_pd)
            : cpu_convolution_bwd_data_pd_t(engine, adesc, attr, hint_fwd_pd)
            , jcp_(), rtus_() {}

        DECLARE_COMMON_PD_T(
                JIT_IMPL_NAME_HELPER("jit_1x1:", avx2, ""),
                jit_avx2_1x1_convolution_bwd_data_t);

        virtual status_t init() override {
            using namespace prop_kind;
            assert(this->engine()->kind() == engine_kind::cpu);
            bool ok = true
                && this->set_default_params() == status::success
                && this->desc()->prop_kind == backward_data
                && this->desc()->alg_kind == alg_kind::convolution_direct
                && !this->has_zero_dim_memory()
                && utils::everyone_is(data_type::f32,
                        this->desc()->diff_src_desc.data_type,
                        this->desc()->weights_desc.data_type,
                        this->desc()->diff_dst_desc.data_type);
            if (!ok) return status::unimplemented;

            const convolution_desc_t *conv_d = this->desc();
            const memory_desc_t *diff_src_d = this->diff_src_pd_.desc();
            rtus_prepare(this, conv_d, diff_src_d, this->diff_dst_pd_.desc());

            return jit_avx2_1x1_conv_kernel_f32::init_conf(jcp_, *conv_d,
                    *diff_src_d, *this->weights_pd_.desc(),
                    *this->diff_dst_pd_.desc(), *this->attr());
        }

        // TODO (Roma): structs conf header cleanup
        jit_1x1_conv_conf_t jcp_;
        struct reduce_to_unit_stride_t {
            convolution_desc_t conv_d_;
            bool reduce_src_;
        } rtus_;

    protected:
        virtual status_t set_default_params() override {
            using namespace memory_format;

            if (this->diff_src_pd_.desc()->format == any)
                CHECK(this->diff_src_pd_.set_format(utils::pick(
                    this->ndims() - 3, nCw8c, nChw8c)));
            if (this->diff_dst_pd_.desc()->format == any)
                CHECK(this->diff_dst_pd_.set_format(utils::pick(
                    this->ndims() - 3, nCw8c, nChw8c)));
            if (this->weights_pd_.desc()->format == any)
                CHECK(this->weights_pd_.set_format(this->with_groups()
                    ? utils::pick(this->ndims() - 3, gOIw8o8i, gOIhw8o8i)
                    : utils::pick(this->ndims() - 3, OIw8o8i, OIhw8o8i)));
            return status::success;
        }
    };

    template <cpu_isa_t isa, typename conv_t>
    friend void init_rtus_driver(conv_t *self);

    jit_avx2_1x1_convolution_bwd_data_t(const pd_t *pd,
            const input_vector &inputs, const output_vector &outputs)
        : cpu_primitive_t(&conf_, inputs, outputs), conf_(*pd)
        , kernel_(nullptr), rtus_driver_(nullptr), ws_per_thread_(0)
        , scratch_(nullptr)
    {
        kernel_ = new jit_avx2_1x1_conv_kernel_f32(conf_.jcp_, *conf_.attr());
        init_rtus_driver<avx2>(this);
    }
    ~jit_avx2_1x1_convolution_bwd_data_t() {
        delete kernel_;
        delete rtus_driver_;
        free(scratch_);
    }

    typedef typename prec_traits<data_type::f32>::type data_t;

    virtual void execute(event_t *e) {
        switch (conf_.desc()->prop_kind) {
        case prop_kind::backward_data:
            execute_backward_data();
            break;
        default:
            assert(!"invalid prop_kind");
        }
        e->set_state(event_t::ready);
    }

private:
    void execute_backward_data();
    pd_t conf_;
    jit_avx2_1x1_conv_kernel_f32 *kernel_;

    /* reduction to unit stride */
    rtus_driver_t<avx2> *rtus_driver_;
    size_t ws_per_thread_;
    data_t *scratch_;
};

struct jit_avx2_1x1_convolution_bwd_weights_t: public cpu_primitive_t {
    struct pd_t: public cpu_convolution_bwd_weights_pd_t {
        pd_t(engine_t *engine,
                const convolution_desc_t *adesc,
                const primitive_attr_t *attr,
                const convolution_fwd_pd_t *hint_fwd_pd)
            : cpu_convolution_bwd_weights_pd_t(engine, adesc, attr, hint_fwd_pd)
            , jcp_(), rtus_() {}

        DECLARE_COMMON_PD_T(
                JIT_IMPL_NAME_HELPER("jit_1x1:", avx2, ""),
                jit_avx2_1x1_convolution_bwd_weights_t);

        virtual status_t init() override {
            using namespace prop_kind;
            assert(this->engine()->kind() == engine_kind::cpu);
            bool ok = true
                && this->set_default_params() == status::success
                && this->desc()->prop_kind == backward_weights
                && this->desc()->alg_kind == alg_kind::convolution_direct
                && !this->has_zero_dim_memory()
                && utils::everyone_is(data_type::f32,
                        this->desc()->src_desc.data_type,
                        this->desc()->diff_weights_desc.data_type,
                        this->desc()->diff_dst_desc.data_type)
                && IMPLICATION(this->with_bias(),
                        data_type::f32 == desc()->diff_bias_desc.data_type);
            if (!ok) return status::unimplemented;

            const convolution_desc_t *conv_d = this->desc();
            const memory_desc_t *src_d = this->src_pd_.desc();
            rtus_prepare(this, conv_d, src_d, this->diff_dst_pd_.desc());

            return jit_avx2_1x1_conv_kernel_f32::init_conf(jcp_, *conv_d,
                    *src_d, *this->diff_weights_pd_.desc(),
                    *this->diff_dst_pd_.desc(), *this->attr());
        }

        // TODO (Roma): structs conf header cleanup
        jit_1x1_conv_conf_t jcp_;

        struct reduce_to_unit_stride_t {
            convolution_desc_t conv_d_;
            bool reduce_src_;
        } rtus_;

    protected:
        virtual status_t set_default_params() override {
            using namespace memory_format;

            if (this->src_pd_.desc()->format == any)
                CHECK(this->src_pd_.set_format(utils::pick(this->ndims() - 3,
                    nCw8c, nChw8c)));
            if (this->diff_dst_pd_.desc()->format == any)
                CHECK(this->diff_dst_pd_.set_format(utils::pick(
                    this->ndims() - 3, nCw8c, nChw8c)));
            if (this->diff_weights_pd_.desc()->format == any)
                CHECK(this->diff_weights_pd_.set_format(this->with_groups()
                    ? utils::pick(this->ndims() - 3, gOIw8i8o, gOIhw8i8o)
                    : utils::pick(this->ndims() - 3, OIw8i8o, OIhw8i8o)));
            if (this->diff_bias_pd_.desc()->format == any)
                CHECK(this->diff_bias_pd_.set_format(x));
            return status::success;
        }
    };

    template <cpu_isa_t isa, typename conv_t>
    friend void init_rtus_driver(conv_t *self);

    jit_avx2_1x1_convolution_bwd_weights_t(const pd_t *pd,
            const input_vector &inputs, const output_vector &outputs);
    ~jit_avx2_1x1_convolution_bwd_weights_t() {
        delete kernel_;
        delete rtus_driver_;
        delete reducer_weights_;
        delete reducer_bias_;
        free(scratch_);
        free(padded_bias_);
    }

    typedef typename prec_traits<data_type::f32>::type data_t;

    virtual void execute(event_t *e) {
        switch (conf_.desc()->prop_kind) {
        case prop_kind::backward_weights:
            execute_backward_weights();
            break;
        default:
            assert(!"invalid prop_kind");
        }
        e->set_state(event_t::ready);
    }

private:
    void execute_backward_weights();
    pd_t conf_;
    jit_avx2_1x1_conv_kernel_f32 *kernel_;
    cpu_reducer_2d_t<data_type::f32> *reducer_weights_;
    cpu_reducer_t<data_type::f32> *reducer_bias_;

    /* reduction to unit stride */
    rtus_driver_t<avx2> *rtus_driver_;
    size_t ws_per_thread_;
    data_t *scratch_;
    data_t *padded_bias_;
};

}
}
}

#endif
