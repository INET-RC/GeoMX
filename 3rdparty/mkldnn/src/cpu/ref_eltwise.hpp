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

#ifndef CPU_REF_ELTWISE_HPP
#define CPU_REF_ELTWISE_HPP

#include <assert.h>

#include "c_types_map.hpp"
#include "cpu_eltwise_pd.hpp"
#include "cpu_engine.hpp"
#include "type_helpers.hpp"
#include "utils.hpp"

namespace mkldnn {
namespace impl {
namespace cpu {

template <impl::data_type_t data_type>
struct ref_eltwise_fwd_t: public cpu_primitive_t {
    struct pd_t: public cpu_eltwise_fwd_pd_t {
        pd_t(engine_t *engine, const eltwise_desc_t *adesc,
                const primitive_attr_t *attr,
                const eltwise_fwd_pd_t *hint_fwd_pd)
            : cpu_eltwise_fwd_pd_t(engine, adesc, attr, hint_fwd_pd) {}

        DECLARE_COMMON_PD_T("ref:any", ref_eltwise_fwd_t);

        virtual status_t init() override {
            using namespace prop_kind;
            using namespace memory_format;
            using namespace utils;
            assert(engine()->kind() == engine_kind::cpu);

            auto src_d = memory_desc_wrapper(src_pd());

            use_dense_ = false
                || src_d.is_dense()
                || (src_d.is_dense(true) && is_zero_preserved());

            use_nCspBc_padded_ = !use_dense_
                && one_of(desc()->data_desc.format, nChw8c, nChw16c,
                    nCdhw8c, nCdhw16c)
                && src_d.only_padded_dim(1)
                && src_d.is_dense(true);

            if (has_zero_dim_memory())
                use_dense_ = use_nCspBc_padded_ = false;

            const bool use_generic = !use_dense_ && !use_nCspBc_padded_;

            bool ok = true
                && one_of(desc()->prop_kind, forward_training,
                        forward_inference)
                && everyone_is(data_type, desc()->data_desc.data_type)
                && IMPLICATION(use_generic, one_of(src_d.ndims(), 4, 5))
                && attr()->has_default_values();
            if (!ok) return status::unimplemented;

            return status::success;
        }

        bool use_dense_, use_nCspBc_padded_;
    };

    ref_eltwise_fwd_t(const pd_t *pd, const input_vector &inputs,
            const output_vector &outputs)
        : cpu_primitive_t(&conf_, inputs, outputs), conf_(*pd) {}
    typedef typename prec_traits<data_type>::type data_t;

    virtual void execute(event_t *e) {
        if (conf_.use_dense_)
            execute_forward_dense();
        else if (conf_.use_nCspBc_padded_)
            execute_forward_nCspBc_padded();
        else
            execute_forward_generic();
        e->set_state(event_t::ready);
    }

private:
    void execute_forward_nCspBc_padded();
    void execute_forward_dense();
    void execute_forward_generic();
    pd_t conf_;
};

template <impl::data_type_t data_type>
struct ref_eltwise_bwd_t: public cpu_primitive_t {
    struct pd_t: public cpu_eltwise_bwd_pd_t {
        pd_t(engine_t *engine, const eltwise_desc_t *adesc,
                const primitive_attr_t *attr,
                const eltwise_fwd_pd_t *hint_fwd_pd)
            : cpu_eltwise_bwd_pd_t(engine, adesc, attr, hint_fwd_pd) {}

        DECLARE_COMMON_PD_T("ref:any", ref_eltwise_bwd_t);

        virtual status_t init() override {
            using namespace prop_kind;
            using namespace utils;
            assert(engine()->kind() == engine_kind::cpu);
            bool ok = true && desc()->prop_kind == backward_data
                    && everyone_is(data_type, desc()->data_desc.data_type,
                            desc()->diff_data_desc.data_type)
                    && attr()->has_default_values();
            if (!ok) return status::unimplemented;

            auto diff_dst_d = memory_desc_wrapper(diff_dst_pd());
            const bool same_fmt_ = diff_dst_d == memory_desc_wrapper(src_pd());

            use_dense_ = true
                && same_fmt_
                && diff_dst_d.is_dense(true)
                && is_zero_preserved()
                && !has_zero_dim_memory();
            const bool use_generic = !use_dense_;

            if (use_generic && !one_of(diff_dst_d.ndims(), 4, 5))
                return status::unimplemented;

            return status::success;
        }

        bool use_dense_;
    };

    ref_eltwise_bwd_t(const pd_t *pd, const input_vector &inputs,
            const output_vector &outputs)
        : cpu_primitive_t(&conf_, inputs, outputs), conf_(*pd) {}
    typedef typename prec_traits<data_type>::type data_t;

    virtual void execute(event_t *e) {
        if (conf_.use_dense_) execute_backward_dense();
        else execute_backward_generic();
        e->set_state(event_t::ready);
    }

private:
    void execute_backward_dense();
    void execute_backward_generic();
    pd_t conf_;
};

}
}
}

#endif

// vim: et ts=4 sw=4 cindent cino^=l0,\:0,N-s
