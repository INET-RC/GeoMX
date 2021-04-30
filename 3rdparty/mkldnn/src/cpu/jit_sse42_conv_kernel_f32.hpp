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

#ifndef JIT_SSE42_CONV_KERNEL_F32_HPP
#define JIT_SSE42_CONV_KERNEL_F32_HPP

#include "c_types_map.hpp"
#include "jit_generator.hpp"
#include "jit_primitive_conf.hpp"
#include "cpu_memory.hpp"

namespace mkldnn {
namespace impl {
namespace cpu {

struct jit_sse42_conv_fwd_kernel_f32: public jit_generator {
    jit_sse42_conv_fwd_kernel_f32(jit_conv_conf_t ajcp,
            const primitive_attr_t &attr): jcp(ajcp), attr_(attr)
    {
        this->generate();
        jit_ker = (void (*)(jit_conv_call_s *))this->getCode();
    }

    static bool post_ops_ok(jit_conv_conf_t &jcp,
            const primitive_attr_t &attr);

    static status_t init_conf(jit_conv_conf_t &jcp,
            const convolution_desc_t &cd, const memory_desc_wrapper &src_d,
            const memory_desc_wrapper &weights_d,
            const memory_desc_wrapper &dst_d, const primitive_attr_t &attr,
            bool with_relu = false, float relu_negative_slope = 0.);

    DECLARE_CPU_JIT_AUX_FUNCTIONS(jit_sse42_conv_fwd_kernel_f32)
    jit_conv_conf_t jcp;
    const primitive_attr_t &attr_;
    void (*jit_ker)(jit_conv_call_s *);

private:
    using reg64_t = const Xbyak::Reg64;
    reg64_t reg_input = rax;
    reg64_t aux_reg_input = r8;
    reg64_t reg_kernel = rdx;
    reg64_t aux_reg_kernel = r9;
    reg64_t reg_output = rsi;
    reg64_t reg_bias = rbx;

    reg64_t kj = r10;
    reg64_t oi_iter = r11;
    reg64_t ki_iter = r12;
    reg64_t reg_kh = abi_not_param1;
    reg64_t simd_iter = r15;
    reg64_t reg_oc_blocks = r14;
    reg64_t imm_addr64 = reg_oc_blocks;
    Xbyak::Reg32 reg_ci_flag = r13d;
    Xbyak::Xmm xmm_relu_ns = Xbyak::Xmm(14);
    Xbyak::Xmm xmm_res_ns = Xbyak::Xmm(13);
    Xbyak::Xmm xzero = Xbyak::Xmm(15);
    Xbyak::Xmm xmask = Xbyak::Xmm(0);

    inline void oh_step_unroll_kw(int ur_w, int pad_l, int pad_r,
            int oc_blocks);
    inline void oh_step_nopad(int ur_w, int pad_l, int pad_r, int oc_blocks);
    inline void width_blk_step(int ur_w, int pad_l, int pad_r, int oc_blocks);
    inline void solve_common(int oc_blocks);

    void generate();
};

}
}
}

#endif
