/*******************************************************************************
* Copyright 2018 Intel Corporation
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

#include "c_types_map.hpp"
#include "nstl.hpp"
#include "type_helpers.hpp"
#include "utils.hpp"
#include "cpu_memory.hpp"

#include "jit_uni_dw_conv_kernel_f32.hpp"

#define GET_OFF(field) offsetof(jit_conv_call_s, field)

namespace mkldnn {
namespace impl {
namespace cpu {

using namespace mkldnn::impl::prop_kind;
using namespace mkldnn::impl::memory_format;
using namespace mkldnn::impl::utils;

using namespace Xbyak;

template <cpu_isa_t isa>
void jit_uni_dw_conv_fwd_kernel_f32<isa>::load_src(int ur_ch_blocks, int ur_w) {
    int repeats = isa == sse42 ? 2 : 1;
    for (int i = 0; i < repeats; i++) {
        for (int ch = 0; ch < ur_ch_blocks; ch++) {
            for (int ow = 0; ow < ur_w; ow++) {
                Vmm vmm_acc = get_acc_reg(i*ur_ch_blocks*ur_w + ch*ur_w + ow);

                int b_off = ch*jcp.ch_block + i*4;
                if (this->jcp.with_bias)
                    uni_vmovups(vmm_acc,
                        vmmword[reg_bias + b_off*sizeof(float)]);
                else
                    uni_vpxor(vmm_acc, vmm_acc, vmm_acc);

                int o_off = ch*jcp.oh*jcp.ow*jcp.ch_block
                    + ow*jcp.ch_block + i*4;
                if (this->jcp.with_sum)
                    uni_vaddps(vmm_acc, vmm_acc,
                        vmmword[reg_output + o_off*sizeof(float)]);
            }
        }
    }
}

template <cpu_isa_t isa>
void jit_uni_dw_conv_fwd_kernel_f32<isa>::apply_filter(
        int ur_ch_blocks, int ur_w) {
    int ch_blk = jcp.ch_block;
    int dilate_h = jcp.dilate_h + 1;
    int dilate_w = jcp.dilate_w + 1;
    int stride_w = jcp.stride_w;

    Label iter_exit_label;

    cmp(reg_kh, 0);
    je(iter_exit_label, T_NEAR);
    cmp(reg_kw, 0);
    je(iter_exit_label, T_NEAR);

    mov(iter_kh, reg_kh);
    Label kh_label;
    L(kh_label); {
        mov(iter_kw, reg_kw);
        mov(aux1_reg_input, aux_reg_input);
        mov(aux1_reg_kernel, aux_reg_kernel);

        Label kw_label;
        L(kw_label); {
            int repeats = isa == sse42 ? 2 : 1;
            for (int i = 0; i < repeats; i++) {
                for (int ch = 0; ch < ur_ch_blocks; ch++) {
                    int ker_off = ch*jcp.kh*jcp.kw*ch_blk + i*4;
                    Vmm vmm_ker = get_ker_reg(0);
                    uni_vmovups(vmm_ker, ptr[aux1_reg_kernel
                        + ker_off*sizeof(float)]);

                    for (int ow = 0; ow < ur_w; ow++) {
                        int inp_off = ch*jcp.ih*jcp.iw*ch_blk
                            + ow*stride_w*ch_blk + i*4;
                        Vmm vmm_src = get_src_reg(0);
                        uni_vmovups(vmm_src, ptr[aux1_reg_input
                            + inp_off*sizeof(float)]);

                        Vmm vmm_acc = get_acc_reg(i*ur_ch_blocks*ur_w
                            + ch*ur_w + ow);
                        uni_vfmadd231ps(vmm_acc, vmm_src, vmm_ker);
                    }
                }
            }
            add(aux1_reg_kernel, ch_blk*sizeof(float));
            add(aux1_reg_input, ch_blk*dilate_w*sizeof(float));

            dec(iter_kw);
            cmp(iter_kw, 0);
            jg(kw_label, T_NEAR);
        }
        add(aux_reg_kernel, jcp.kw*ch_blk*sizeof(float));
        add(aux_reg_input, jcp.iw*ch_blk*dilate_h*sizeof(float));

        dec(iter_kh);
        cmp(iter_kh, 0);
        jg(kh_label, T_NEAR);
    }

    L(iter_exit_label);
}

template <cpu_isa_t isa>
void jit_uni_dw_conv_fwd_kernel_f32<isa>::apply_filter_unrolled(
        int ur_ch_blocks, int ur_w) {
    int ch_blk = jcp.ch_block;
    int dilate_h = jcp.dilate_h + 1;
    int dilate_w = jcp.dilate_w + 1;
    int stride_w = jcp.stride_w;

    Label iter_exit_label;

    cmp(reg_kh, 0);
    je(iter_exit_label, T_NEAR);

    mov(iter_kh, reg_kh);
    Label kh_label;
    L(kh_label); {
        int repeats = isa == sse42 ? 2 : 1;
        for (int i = 0; i < repeats; i++) {
            for (int ch = 0; ch < ur_ch_blocks; ch++) {
                for (int kw = 0; kw < jcp.kw; kw++) {
                    int ker_off = ch*jcp.kh*jcp.kw*ch_blk + kw*ch_blk + i*4;

                    Vmm vmm_ker = get_ker_reg(0);
                    uni_vmovups(vmm_ker, ptr[aux_reg_kernel
                        + ker_off*sizeof(float)]);

                    for (int ow = 0; ow < ur_w; ow++) {
                        int inp_off = ch*jcp.ih*jcp.iw*ch_blk
                            + ow*stride_w*ch_blk + kw*ch_blk*dilate_w + i*4;

                        Vmm vmm_src = get_src_reg(0);
                        uni_vmovups(vmm_src, ptr[aux_reg_input
                            + inp_off*sizeof(float)]);

                        Vmm vmm_acc = get_acc_reg(i*ur_ch_blocks*ur_w
                            + ch*ur_w + ow);
                        uni_vfmadd231ps(vmm_acc, vmm_src, vmm_ker);
                    }
                }
            }
        }

        add(aux_reg_kernel, jcp.kw*ch_blk*sizeof(float));
        add(aux_reg_input, jcp.iw*ch_blk*dilate_h*sizeof(float));

        dec(iter_kh);
        cmp(iter_kh, 0);
        jg(kh_label, T_NEAR);
    }

    L(iter_exit_label);
}

template <cpu_isa_t isa>
void jit_uni_dw_conv_fwd_kernel_f32<isa>::apply_activation(
        int ur_ch_blocks, int ur_w) {
    if (this->jcp.with_relu) {
        uni_vpxor(vmm_zero, vmm_zero, vmm_zero);
        if (jcp.relu_negative_slope == 0) {
            vmm_relu_ns = vmm_zero;
        } else {
            mov(imm_addr64, float2int(jcp.relu_negative_slope));
            movq(xmm_relu_ns, imm_addr64);
            uni_vbroadcastss(vmm_relu_ns, xmm_relu_ns);
        }

        int repeats = isa == sse42 ? 2 : 1;
        for (int i = 0; i < repeats; i++) {
            for (int ch = 0; ch < ur_ch_blocks; ch++) {
                for (int ow = 0; ow < ur_w; ow++) {
                    Vmm vmm_dst = get_acc_reg(i*ur_ch_blocks*ur_w
                        + ch*ur_w + ow);

                    if (isa == sse42) {
                        pxor(vmm_mask, vmm_mask);
                        cmpps(vmm_mask, vmm_dst, _cmp_gt_os);
                        movups(vmm_res_ns, vmm_dst);
                        mulps(vmm_res_ns, vmm_relu_ns);
                        blendvps(vmm_dst, vmm_res_ns);
                    } else if (isa == avx2) {
                        vcmpgtps(vmm_mask, vmm_dst, vmm_zero);
                        vmulps(vmm_res_ns, vmm_relu_ns, vmm_dst);
                        vblendvps(vmm_dst, vmm_res_ns, vmm_dst, vmm_mask);
                    } else if (isa == avx512_common) {
                        Opmask kmask = Opmask(7);
                        vcmpps(kmask, vmm_dst, vmm_zero, _cmp_lt_os);
                        vmulps(vmm_dst | kmask, vmm_dst, vmm_relu_ns);
                    }
                }
            }
        }
    }
}

template <cpu_isa_t isa>
void jit_uni_dw_conv_fwd_kernel_f32<isa>::store_dst(
        int ur_ch_blocks, int ur_w) {
    int ch_blk = jcp.ch_block;

    int repeats = isa == sse42 ? 2 : 1;
    for (int i = 0; i < repeats; i++) {
        for (int ch = 0; ch < ur_ch_blocks; ch++) {
            for (int ow = 0; ow < ur_w; ow++) {
                int o_off = ch*jcp.oh*jcp.ow*ch_blk + ow*ch_blk + i*4;
                Vmm vmm_dst = get_acc_reg(i*ur_ch_blocks*ur_w + ch*ur_w + ow);

                uni_vmovups(vmmword[reg_output + o_off*sizeof(float)], vmm_dst);
            }
        }
    }
}

template <cpu_isa_t isa>
void jit_uni_dw_conv_fwd_kernel_f32<isa>::loop_body(int ur_ch_blocks) {
    Label unrolled_w_label;
    Label tail_w_label;
    Label exit_label;

    L(unrolled_w_label); {
        int ur_w = jcp.ur_w;

        cmp(reg_ur_w, ur_w);
        jl(tail_w_label, T_NEAR);

        mov(aux_reg_input, reg_input);
        mov(aux_reg_kernel, reg_kernel);

        load_src(ur_ch_blocks, ur_w);
        apply_filter_unrolled(ur_ch_blocks, ur_w);
        apply_activation(ur_ch_blocks, ur_w);
        store_dst(ur_ch_blocks, ur_w);

        add(reg_input, sizeof(float) * ur_w * jcp.ch_block * jcp.stride_w);
        add(reg_output, sizeof(float) * ur_w * jcp.ch_block);

        sub(reg_ur_w, ur_w);
        jmp(unrolled_w_label);
    }

    L(tail_w_label); {
        int ur_w = 1;

        cmp(reg_ur_w, ur_w);
        jl(exit_label, T_NEAR);

        mov(aux_reg_input, reg_input);
        mov(aux_reg_kernel, reg_kernel);

        load_src(ur_ch_blocks, ur_w);
        apply_filter(ur_ch_blocks, ur_w);
        apply_activation(ur_ch_blocks, ur_w);
        store_dst(ur_ch_blocks, ur_w);

        add(reg_input, sizeof(float) * ur_w * jcp.ch_block * jcp.stride_w);
        add(reg_output, sizeof(float) * ur_w * jcp.ch_block);

        sub(reg_ur_w, ur_w);
        jmp(tail_w_label);
    }

    L(exit_label);
}

template <cpu_isa_t isa>
void jit_uni_dw_conv_fwd_kernel_f32<isa>::generate() {
    this->preamble();

    mov(reg_input, ptr[this->param1 + GET_OFF(src)]);
    mov(reg_output, ptr[this->param1 + GET_OFF(dst)]);
    mov(reg_kernel, ptr[this->param1 + GET_OFF(filt)]);
    if (jcp.with_bias)
        mov(reg_bias, ptr[this->param1 + GET_OFF(bias)]);
    mov(reg_kh, ptr[this->param1 + GET_OFF(kh_padding)]);
    mov(reg_kw, ptr[this->param1 + GET_OFF(kw_padding)]);
    mov(reg_ch_blocks, ptr[this->param1 + GET_OFF(ch_blocks)]);
    mov(reg_ur_w, ptr[this->param1 + GET_OFF(ur_w)]);

    Label ch_blocks_tail_label;
    Label exit_label;

    int ch_blocks_tail = jcp.nb_ch % jcp.nb_ch_blocking;

    cmp(reg_ch_blocks, jcp.nb_ch_blocking);
    jne(ch_blocks_tail ? ch_blocks_tail_label : exit_label, T_NEAR);

    loop_body(jcp.nb_ch_blocking); // channel main loop

    if (ch_blocks_tail) {
        L(ch_blocks_tail_label);

        cmp(reg_ch_blocks, ch_blocks_tail);
        jne(exit_label, T_NEAR);

        loop_body(ch_blocks_tail); // channel tail loop
    }

    L(exit_label);

    this->postamble();
}

template <cpu_isa_t isa>
bool jit_uni_dw_conv_fwd_kernel_f32<isa>::post_ops_ok(
        jit_conv_conf_t &jcp, const primitive_attr_t &attr) {
    const auto &p = attr.post_ops_;

    auto is_relu = [&](int idx) { return p.entry_[idx].is_relu(); };
    auto is_sum = [&](int idx) { return p.entry_[idx].is_sum(); };

    switch (p.len_) {
    case 0: return true; // no post_ops
    case 1: return !jcp.with_relu && (is_relu(0) || is_sum(0)); // sum OR relu
    case 2: return !jcp.with_relu && (is_sum(0) && is_relu(1)); // sum->relu
    default: return false;
    }

    return false;
}

template <cpu_isa_t isa>
status_t jit_uni_dw_conv_fwd_kernel_f32<isa>::init_conf(jit_conv_conf_t &jcp,
        const convolution_desc_t &cd, const memory_desc_wrapper &src_d,
        const memory_desc_wrapper &weights_d, const memory_desc_wrapper &dst_d,
        const primitive_attr_t &attr, bool with_relu, float relu_negative_slope)
{
    if (!mayiuse(isa)) return status::unimplemented;

    const int simd_w = isa == avx512_common ? 16 : 8;

    jcp.prop_kind = cd.prop_kind;

    const bool with_groups = weights_d.ndims() == src_d.ndims() + 1;
    if (!with_groups) return status::unimplemented;

    jcp.ngroups = weights_d.dims()[0];
    jcp.mb = src_d.dims()[0];

    jcp.oc = dst_d.dims()[1];
    jcp.oc_without_padding = jcp.oc;
    jcp.ic = src_d.dims()[1];

    jcp.ih = src_d.dims()[2];
    jcp.iw = src_d.dims()[3];
    jcp.oh = dst_d.dims()[2];
    jcp.ow = dst_d.dims()[3];

    jcp.kh = weights_d.dims()[3];
    jcp.kw = weights_d.dims()[4];

    jcp.t_pad = cd.padding[0][0];
    jcp.l_pad = cd.padding[0][1];
    jcp.b_pad = cd.padding[1][0];
    jcp.r_pad = cd.padding[1][1];

    jcp.stride_h = cd.strides[0];
    jcp.stride_w = cd.strides[1];

    jcp.dilate_h = cd.dilates[0];
    jcp.dilate_w = cd.dilates[1];

    jcp.src_fmt = src_d.format();
    jcp.with_bias = cd.bias_desc.format != memory_format::undef;
    jcp.with_relu = with_relu;
    jcp.relu_negative_slope = relu_negative_slope;

    if (!post_ops_ok(jcp, attr))
        return status::unimplemented;

    const auto &p = attr.post_ops_;
    jcp.with_sum = p.find(primitive_kind::sum) != -1;
    if (!jcp.with_relu) {
        int eltwise_ind = p.find(primitive_kind::eltwise);
        if (eltwise_ind != -1) {
            jcp.with_relu  = true;
            jcp.relu_negative_slope = p.entry_[eltwise_ind].eltwise.alpha;
        }
    }

    bool ok_to_pad_channels = true
        && jcp.oc == jcp.ngroups
        && jcp.ic == jcp.ngroups
        && one_of(isa, avx512_common, avx2);
    if (ok_to_pad_channels) {
        jcp.oc = rnd_up(jcp.oc, simd_w);
        jcp.ic = rnd_up(jcp.oc, simd_w);
        jcp.ngroups = rnd_up(jcp.ngroups, simd_w);
    }

    auto desired_act_fmt = isa == avx512_common ? nChw16c : nChw8c;
    auto desired_wei_fmt = isa == avx512_common ? Goihw16g : Goihw8g;

    bool args_ok = true
        && jcp.oc == jcp.ngroups
        && jcp.ic == jcp.ngroups
        && jcp.ngroups % simd_w == 0
        && src_d.format() == desired_act_fmt
        && weights_d.format() == desired_wei_fmt
        && one_of(cd.bias_desc.format, memory_format::undef, any, x)
        && dst_d.format() == desired_act_fmt
        && jcp.ic <= src_d.blocking_desc().padding_dims[1]
        && jcp.oc <= dst_d.blocking_desc().padding_dims[1]
        && jcp.ngroups <= weights_d.blocking_desc().padding_dims[0];
    if (!args_ok) return status::unimplemented;

    jcp.ur_w = isa == avx512_common ? 6 : isa == avx2 ? 4 : 3;

    jcp.ch_block = simd_w;
    jcp.nb_ch = jcp.oc / jcp.ch_block;
    jcp.nb_ch_blocking = isa == avx512_common ? 4 : isa == avx2 ? 3 : 2;
    if (jcp.nb_ch < jcp.nb_ch_blocking)
        jcp.nb_ch_blocking = jcp.nb_ch;

    return status::success;
}

template struct jit_uni_dw_conv_fwd_kernel_f32<avx512_common>;
template struct jit_uni_dw_conv_fwd_kernel_f32<avx2>;
template struct jit_uni_dw_conv_fwd_kernel_f32<sse42>;

template <cpu_isa_t isa>
inline void jit_uni_dw_conv_bwd_data_kernel_f32<isa>::load_ddst(
        int ur_ch_blocks, int ur_str_w) {
    int repeats = isa == sse42 ? 2 : 1;
    for (int i = 0; i < repeats; i++) {
        for (int ch = 0; ch < ur_ch_blocks; ch++) {
            for (int w = 0; w < ur_str_w; w++) {
                Vmm vmm_acc = get_acc_reg(i*ur_ch_blocks*ur_str_w
                    + ch*ur_str_w + w);
                uni_vpxor(vmm_acc, vmm_acc, vmm_acc);
            }
        }
    }
}

template <cpu_isa_t isa>
inline void jit_uni_dw_conv_bwd_data_kernel_f32<isa>::apply_filter(
        int ur_ch_blocks, int ur_str_w) {
    int kw = jcp.kw;
    int kh = jcp.kh;
    int ow = jcp.ow;
    int oh = jcp.oh;

    int ch_blk = jcp.ch_block;
    int stride_h = jcp.stride_h;
    int stride_w = jcp.stride_w;

    Label iter_exit_label;

    cmp(reg_kh, 0);
    je(iter_exit_label, T_NEAR);

    cmp(reg_kw, 0);
    je(iter_exit_label, T_NEAR);

    mov(iter_kh, reg_kh);
    Label kh_label;
    L(kh_label); {
        mov(aux1_reg_ddst, aux_reg_ddst);
        mov(aux1_reg_kernel, aux_reg_kernel);

        mov(iter_kw, reg_kw);
        Label kw_label;
        L(kw_label); {
            int repeats = isa == sse42 ? 2 : 1;
            for (int i = 0; i < repeats; i++) {
                for (int ch = 0; ch < ur_ch_blocks; ch++) {
                    int ker_off = ch*kh*kw*ch_blk + i*4;
                    Vmm vmm_ker = get_ker_reg(0);
                    uni_vmovups(vmm_ker, ptr[aux1_reg_kernel
                        + ker_off*sizeof(float)]);

                    for (int w = 0; w < ur_str_w; w++) {
                        int ddst_off = (ch*oh*ow + w)*ch_blk + i*4;

                        Vmm vmm_src = get_src_reg(0);
                        uni_vmovups(vmm_src, ptr[aux1_reg_ddst
                            + ddst_off*sizeof(float)]);

                        Vmm vmm_acc = get_acc_reg(i*ur_ch_blocks*ur_str_w
                            + ch*ur_str_w + w);
                        uni_vfmadd231ps(vmm_acc, vmm_src, vmm_ker);
                    }
                }
            }

            add(aux1_reg_kernel, ch_blk*stride_w*sizeof(float));
            sub(aux1_reg_ddst, ch_blk*sizeof(float));

            sub(iter_kw, stride_w);
            cmp(iter_kw, 0);
            jg(kw_label, T_NEAR);
        }

        add(aux_reg_kernel, kw*ch_blk*stride_h*sizeof(float));
        sub(aux_reg_ddst, ow*ch_blk*sizeof(float));

        sub(iter_kh, stride_h);
        cmp(iter_kh, 0);
        jg(kh_label, T_NEAR);
    }

    L(iter_exit_label);
}

template <cpu_isa_t isa>
inline void jit_uni_dw_conv_bwd_data_kernel_f32<isa>::store_dsrc(
        int ur_ch_blocks, int ur_str_w) {
    int ch_blk = jcp.ch_block;
    int iw = jcp.iw;
    int ih = jcp.ih;
    int stride_w = jcp.stride_w;

    int repeats = isa == sse42 ? 2 : 1;
    for (int i = 0; i < repeats; i++) {
        for (int ch = 0; ch < ur_ch_blocks; ch++) {
            for (int w = 0; w < ur_str_w; w++) {
                int dsrc_off = (ch*ih*iw + w*stride_w)*ch_blk + i*4;
                Vmm vmm_acc = get_acc_reg(i*ur_ch_blocks*ur_str_w
                    + ch*ur_str_w + w);

                uni_vmovups(ptr[reg_dsrc + dsrc_off*sizeof(float)], vmm_acc);
            }
        }
    }
}

template <cpu_isa_t isa>
inline void jit_uni_dw_conv_bwd_data_kernel_f32<isa>::loop_body(
        int ur_ch_blocks) {
    Label unrolled_w_label;
    Label tail_w_label;
    Label exit_label;

    L(unrolled_w_label); {
        int ur_w = jcp.ur_w;

        cmp(reg_ur_str_w, ur_w);
        jl(tail_w_label, T_NEAR);

        mov(aux_reg_ddst, reg_ddst);
        mov(aux_reg_kernel, reg_kernel);

        load_ddst(ur_ch_blocks, ur_w);
        apply_filter(ur_ch_blocks, ur_w);
        store_dsrc(ur_ch_blocks, ur_w);

        add(reg_dsrc, sizeof(float) * ur_w * jcp.ch_block * jcp.stride_w);
        add(reg_ddst, sizeof(float) * ur_w * jcp.ch_block);

        sub(reg_ur_str_w, ur_w);
        jmp(unrolled_w_label);
    }

    L(tail_w_label); {
        int ur_w = 1;

        cmp(reg_ur_str_w, ur_w);
        jl(exit_label, T_NEAR);

        mov(aux_reg_ddst, reg_ddst);
        mov(aux_reg_kernel, reg_kernel);

        load_ddst(ur_ch_blocks, ur_w);
        apply_filter(ur_ch_blocks, ur_w);
        store_dsrc(ur_ch_blocks, ur_w);

        add(reg_dsrc, sizeof(float) * ur_w * jcp.ch_block * jcp.stride_w);
        add(reg_ddst, sizeof(float) * ur_w * jcp.ch_block);

        sub(reg_ur_str_w, ur_w);
        jmp(tail_w_label);
    }

    L(exit_label);
}

template <cpu_isa_t isa>
void jit_uni_dw_conv_bwd_data_kernel_f32<isa>::generate() {
    preamble();

    mov(reg_dsrc, ptr[this->param1 + GET_OFF(src)]);
    mov(reg_ddst, ptr[this->param1 + GET_OFF(dst)]);
    mov(reg_kernel, ptr[this->param1 + GET_OFF(filt)]);
    mov(reg_kh, ptr[this->param1 + GET_OFF(kh_padding)]);
    mov(reg_kw, ptr[this->param1 + GET_OFF(kw_padding)]);
    mov(reg_ch_blocks, ptr[this->param1 + GET_OFF(ch_blocks)]);
    mov(reg_ur_str_w, ptr[this->param1 + GET_OFF(ur_str_w)]);

    Label ch_blocks_tail_label;
    Label exit_label;

    int ch_blocks_tail = jcp.nb_ch % jcp.nb_ch_blocking;

    cmp(reg_ch_blocks, jcp.nb_ch_blocking);
    jne(ch_blocks_tail ? ch_blocks_tail_label : exit_label, T_NEAR);

    loop_body(jcp.nb_ch_blocking); // channel main loop

    if (ch_blocks_tail) {
        L(ch_blocks_tail_label);

        cmp(reg_ch_blocks, ch_blocks_tail);
        jne(exit_label, T_NEAR);

        loop_body(ch_blocks_tail); // channel tail loop
    }

    L(exit_label);

    this->postamble();
}

template <cpu_isa_t isa>
status_t jit_uni_dw_conv_bwd_data_kernel_f32<isa>::init_conf(
        jit_conv_conf_t &jcp, const convolution_desc_t &cd,
        const memory_desc_wrapper &diff_src_d,
        const memory_desc_wrapper &weights_d,
        const memory_desc_wrapper &diff_dst_d) {
    if (!mayiuse(isa)) return status::unimplemented;

    const int simd_w = isa == avx512_common ? 16 : 8;

    const bool with_groups = weights_d.ndims() == diff_src_d.ndims() + 1;
    if (!with_groups) return status::unimplemented;

    jcp.ngroups = weights_d.dims()[0];
    jcp.mb = diff_src_d.dims()[0];

    jcp.oc = diff_dst_d.dims()[1];
    jcp.oc_without_padding = jcp.oc;
    jcp.ic = diff_src_d.dims()[1];

    jcp.ih = diff_src_d.dims()[2];
    jcp.iw = diff_src_d.dims()[3];
    jcp.oh = diff_dst_d.dims()[2];
    jcp.ow = diff_dst_d.dims()[3];

    jcp.kh = weights_d.dims()[3];
    jcp.kw = weights_d.dims()[4];

    jcp.t_pad = cd.padding[0][0];
    jcp.l_pad = cd.padding[0][1];
    jcp.b_pad = cd.padding[1][0];
    jcp.r_pad = cd.padding[1][1];

    jcp.stride_h = cd.strides[0];
    jcp.stride_w = cd.strides[1];

    jcp.dilate_h = cd.dilates[0];
    jcp.dilate_w = cd.dilates[1];

    jcp.ihp = jcp.ih + jcp.t_pad + jcp.b_pad;
    jcp.iwp = jcp.iw + jcp.l_pad + jcp.r_pad;

    jcp.src_fmt = diff_src_d.format();

    bool ok_to_pad_channels = true
        && jcp.oc == jcp.ngroups
        && jcp.ic == jcp.ngroups
        && one_of(isa, avx512_common, avx2);
    if (ok_to_pad_channels) {
        jcp.oc = rnd_up(jcp.oc, simd_w);
        jcp.ic = rnd_up(jcp.oc, simd_w);
        jcp.ngroups = rnd_up(jcp.ngroups, simd_w);
    }

    auto desired_act_fmt = isa == avx512_common ? nChw16c : nChw8c;
    auto desired_wei_fmt = isa == avx512_common ? Goihw16g : Goihw8g;

    bool args_ok = true
        && jcp.oc == jcp.ngroups
        && jcp.ic == jcp.ngroups
        && jcp.ngroups % simd_w == 0
        && jcp.dilate_h == 0
        && jcp.dilate_w == 0
        && diff_src_d.format() == desired_act_fmt
        && weights_d.format() == desired_wei_fmt
        && diff_dst_d.format() == desired_act_fmt
        && jcp.oh == (jcp.ihp - jcp.kh) / jcp.stride_h + 1
        && jcp.ow == (jcp.iwp - jcp.kw) / jcp.stride_w + 1
        && jcp.ic <= diff_src_d.blocking_desc().padding_dims[1]
        && jcp.oc <= diff_dst_d.blocking_desc().padding_dims[1]
        && jcp.ngroups <= weights_d.blocking_desc().padding_dims[0];
    if (!args_ok) return status::unimplemented;

    jcp.ur_w = isa == avx512_common ? 6 : isa == avx2 ? 4 : 3;

    jcp.ch_block = simd_w;
    jcp.nb_ch = jcp.ic / jcp.ch_block;
    jcp.nb_ch_blocking = isa == avx512_common ? 4 : isa == avx2 ? 3 : 2;
    if (jcp.nb_ch < jcp.nb_ch_blocking)
        jcp.nb_ch_blocking = jcp.nb_ch;

    return status::success;
}

template struct jit_uni_dw_conv_bwd_data_kernel_f32<avx512_common>;
template struct jit_uni_dw_conv_bwd_data_kernel_f32<avx2>;
template struct jit_uni_dw_conv_bwd_data_kernel_f32<sse42>;

template <cpu_isa_t isa>
inline void jit_uni_dw_conv_bwd_weights_kernel_f32<isa>::zero_filter() {
    for (int r = 0; r < reg_repeats; ++r) {
        for (int i = 0; i < jcp.kw; ++i) {
            Vmm vmm_acc = get_acc_reg(r * jcp.kw + i);
            uni_vpxor(vmm_acc, vmm_acc, vmm_acc);
        }
    }
}

template <cpu_isa_t isa>
inline void jit_uni_dw_conv_bwd_weights_kernel_f32<isa>::load_filter() {
    for (int r = 0; r < reg_repeats; ++r) {
        const int reg_set = r * jcp.kw;
        for (int i = 0; i < jcp.kw; ++i) {
            int off_filter = (reg_set + i) * simd_w;
            Vmm vmm_acc = get_acc_reg(reg_set + i);
            uni_vmovups(vmm_acc,
                    vmmword[tmp_reg_filter + off_filter * sizeof(float)]);
        }
    }
}

template <cpu_isa_t isa>
inline void jit_uni_dw_conv_bwd_weights_kernel_f32<isa>::zero_bias() {
    for (int r = 0; r < reg_repeats; ++r) {
        Vmm vmm_bias = get_bias_reg(r);
        uni_vpxor(vmm_bias, vmm_bias, vmm_bias);
    }
}

template <cpu_isa_t isa>
inline void jit_uni_dw_conv_bwd_weights_kernel_f32<isa>::load_bias() {
    for (int r = 0; r < reg_repeats; ++r) {
        Vmm vmm_bias = get_bias_reg(r);
        uni_vmovups(
                vmm_bias, vmmword[reg_bias_baddr + r * simd_w * sizeof(float)]);
    }
}

template <cpu_isa_t isa>
inline void jit_uni_dw_conv_bwd_weights_kernel_f32<isa>::compute_ow_step_unroll(
        int l_pad, int r_pad, int pad_offset, int ow_block) {
    const int pad = nstl::max(jcp.l_pad, jcp.r_pad);
    const int iw_overlap = jcp.iw + jcp.kw - 1 - jcp.l_pad - jcp.r_pad;
    const int unroll_w = nstl::min(jcp.ur_w, iw_overlap);
    const int right_border = iw_overlap - ow_block;

    /* preamble count for number of cascaded LOAD + FMA operation */
    const int input_preamble_count
            = nstl::max(jcp.kw - jcp.stride_w - l_pad, 0);

    /* LOAD initial input registers, then cascade LOADs and FMAs*/
    for (int r = 0; r < reg_repeats; ++r) {
        for (int i = 0; i < input_preamble_count; i++) {
            int off_input = ((i - pad_offset) * reg_repeats + r) * simd_w;
            Vmm vmm_input = get_input_reg((i + l_pad) * reg_repeats + r);
            uni_vmovups(vmm_input,
                    ptr[tmp_reg_idx_input + off_input * sizeof(float)]);
        }

        for (int i = 0; i < unroll_w; ++i) {
            int off_output = (i * reg_repeats + r) * simd_w;
            Vmm vmm_output = get_output_reg(r);
            uni_vmovups(vmm_output,
                    ptr[tmp_reg_idx_output + off_output * sizeof(float)]);

            int input_load_overlap = i * jcp.stride_w + input_preamble_count;

            /* Cascade 'input' loads for the corresponding FMAs */
            const int cascade_input = nstl::min(jcp.stride_w, jcp.kw);
            for (int c = 0; c < cascade_input; ++c) {
                int off_input
                        = ((c + input_load_overlap - pad_offset) * reg_repeats
                                  + r)
                        * simd_w;
                Vmm vmm_input = get_input_reg(
                        ((c + input_load_overlap + l_pad) % jcp.kw)
                                * reg_repeats
                        + r);
                uni_vmovups(vmm_input,
                        ptr[tmp_reg_idx_input + off_input * sizeof(float)]);
            }

            for (int j = 0; j < jcp.kw; ++j) {

                /* Don't apply FMAs that fall into the padded region */
                if (i + j < l_pad || i + j - pad >= right_border)
                    continue;
                Vmm vmm_input = get_input_reg(
                        ((i * jcp.stride_w + j) % jcp.kw) * reg_repeats + r);
                Vmm vmm_acc = get_acc_reg(j * reg_repeats + r);
                Vmm vmm_aux = isa == sse42 ? get_aux_reg() : vmm_input;
                if( isa == sse42 ) uni_vmovups(vmm_aux, vmm_input);
                uni_vfmadd231ps(vmm_acc, vmm_aux, vmm_output);
            }
        }
    }
}

template <cpu_isa_t isa>
inline void
jit_uni_dw_conv_bwd_weights_kernel_f32<isa>::compute_bias_step_unroll(
        const int unroll_w) {
    for (int r = 0; r < reg_repeats; ++r) {
        for (int i = 0; i < unroll_w; ++i) {
            Vmm vmm_bias = get_bias_reg(r);
            int off_output = (i * reg_repeats + r) * simd_w;
            uni_vaddps(vmm_bias, vmm_bias,
                    vmmword[tmp_reg_idx_output + off_output * sizeof(float)]);
        }
    }
}

template <cpu_isa_t isa>
inline void jit_uni_dw_conv_bwd_weights_kernel_f32<isa>::store_filter() {
    for (int r = 0; r < reg_repeats; ++r) {
        const int reg_set = r * jcp.kw;
        for (int i = 0; i < jcp.kw; ++i) {
            int off_filter = (i + reg_set) * simd_w;
            Vmm vmm_acc = get_acc_reg(i + reg_set);
            uni_vmovups(vmmword[tmp_reg_filter + off_filter * sizeof(float)],
                    vmm_acc);
        }
    }
}

template <cpu_isa_t isa>
inline void jit_uni_dw_conv_bwd_weights_kernel_f32<isa>::store_bias() {
    for (int r = 0; r < reg_repeats; ++r) {
        Vmm vmm_bias = get_bias_reg(r);
        uni_vmovups(
                vmmword[reg_bias_baddr + r * simd_w * sizeof(float)], vmm_bias);
    }
}

template <cpu_isa_t isa>
inline void jit_uni_dw_conv_bwd_weights_kernel_f32<isa>::create_h_bounds_table() {
    /* Bounds are stored on an 8-bit sized element.
     * XXX: potential issues if bounds exceed 255.
     */
    const bool handle_padding = (jcp.t_pad > 0) || (jcp.b_pad > 0);
    if (handle_padding) {

        /* Calculate how many 'h_start' bounds are needed */
        const int h_bounds_count = get_loop_bounds_count(
                nstl::max(jcp.t_pad, jcp.b_pad), jcp.oh, jcp.oh_blk_size);

        align(64);
        L(bound_start_table);
        /* Generate starting bounds for 'oh' loop. This value also determines
         * the overlap (computed as an address offset) between the output over
         * the input for that loop iteration. */
        for (int oh_block = 0; oh_block < h_bounds_count; ++oh_block) {
            for (int kh = 0; kh < jcp.kh; ++kh) {
                te_size start_bound = nstl::max(
                        jcp.t_pad - oh_block * jcp.oh_blk_size - kh, 0);
                write_table(start_bound);
            }
        }
        /* Write offset count for 'input' address calculation. The offset for
         * the input address is conditioned by the 'h' padding intersection over
         * the output rows. */
        for (int kh = 1; kh < jcp.kh; ++kh) {
            te_size kh_accum_value = nstl::max(nstl::min(kh - jcp.t_pad, 1), 0);
            write_table(kh_accum_value);
        }
        /* Last value is not used for offset calculation, write 'nop'
         * equivalent*/
        write_table(0);

        /* Non-padded blocks always increment 'kh' dimension */
        for (int oh_block = 0; oh_block < h_bounds_count - 1; oh_block++) {
            for (int kh = 0; kh < jcp.kh; ++kh) {
                te_size kh_accum_value = 1;
                write_table(kh_accum_value);
            }
        }

        /* number of input elements that overlap over output */
        int ih_overlap = jcp.oh_blk_size + jcp.kh - 1 - jcp.t_pad - jcp.b_pad;

        /* End Bounds for 'oh' default to 'OH' or OH_BLOCK_SIZE, unless
         * the 'oh_block' is within the 'bottom_padding' region. */
        int oh_end_blk = 0;
        for (; oh_end_blk < h_bounds_count - 1; ++oh_end_blk) {
            for (int kh = 0; kh < jcp.kh; ++kh) {
                te_size end_bound = nstl::min((jcp.ih / jcp.stride_h)
                                - jcp.oh_blk_size - oh_end_blk * jcp.oh_blk_size
                                + ih_overlap + 1 - kh,
                        jcp.oh_blk_size);
                write_table(end_bound);
            }
        }
        /* Write bounds for the special case of when 'oh_block' falls within the
         * 'bottom_paddin' region - this always executes since at least 1 row of
         * bounds should exist. */
        const int pad = nstl::max(jcp.b_pad, jcp.t_pad);
        ih_overlap
                = (jcp.ih / jcp.stride_h + jcp.kh - 1 - jcp.t_pad - jcp.b_pad);
        oh_end_blk = jcp.oh - jcp.oh_blk_size;
        for (int kh = 0; kh < jcp.kh; ++kh) {
            te_size end_bound = nstl::min(
                    jcp.oh_blk_size, ih_overlap - oh_end_blk + pad - kh);
            write_table(end_bound);
        }
    }
}

template <cpu_isa_t isa>
inline void jit_uni_dw_conv_bwd_weights_kernel_f32<isa>::compute_bias_loop() {

    Label oh_label;
    Label ow_blk_label;

    const int oh_block_size = jcp.oh_blk_size;
    const int ow_unroll = jcp.ur_w;
    const int ow_block_count = jcp.ow / ow_unroll;
    const int ch_offset = jcp.ch_block;

    mov(tmp_reg_idx_output, reg_output_baddr);

    xor_(iter_oh, iter_oh);
    L(oh_label);
    {

        xor_(iter_ow_blk, iter_ow_blk);
        L(ow_blk_label);
        {

            compute_bias_step_unroll(ow_unroll);

            add(tmp_reg_idx_output, ow_unroll * ch_offset * sizeof(float));

            inc(iter_ow_blk);
            cmp(iter_ow_blk, ow_block_count);
            jl(ow_blk_label, T_NEAR);
        }

        inc(iter_oh);
        cmp(iter_oh, oh_block_size);
        jl(oh_label, T_NEAR);
    }
}

template <cpu_isa_t isa>
inline void jit_uni_dw_conv_bwd_weights_kernel_f32<isa>::compute_kh_loop(
        int l_pad, int r_pad, int pad_offset, bool first_iteration,
        int ow_block) {

    Label kh_label;
    Label oh_label;
    Label exit_innerloop_label;
    Label skip_load_acc;

    const int table_row_count = get_loop_bounds_count(
            nstl::max(jcp.t_pad, jcp.b_pad), jcp.oh, jcp.oh_blk_size);
    const int ih_table_off = 1 * table_row_count * jcp.kh * sizeof(te_size);
    const int end_bound_table_off
            = 2 * table_row_count * jcp.kh * sizeof(te_size);

    const int ch_offset = jcp.ch_block;

    const bool handle_padding = (jcp.t_pad > 0) || (jcp.b_pad > 0);

    mov(tmp_reg_filter, reg_filter_baddr);
    mov(tmp_reg_kh_input, reg_input_baddr);
    xor_(reg_tmp_off, reg_tmp_off);

    if (handle_padding) {
        mov(reg_bound_table_addr, bound_start_table);

        /* move to the row containing the indices for the current 'h' block */
        mov(reg_tmp_off, reg_table_idx);
        imul(reg_tmp_off, reg_tmp_off, jcp.kh * sizeof(unsigned char));
        add(reg_bound_table_addr, reg_tmp_off);
    }

    xor_(iter_kh, iter_kh);
    L(kh_label);
    {

        mov(tmp_reg_idx_output, reg_output_baddr);
        mov(tmp_reg_idx_input, tmp_reg_kh_input);

        if (first_iteration) {

            /* apply zero filter */
            zero_filter();

            /* if zero_filter_flag is set to '1', load filter memory into
             * reg_accum */
            if (jcp.with_bias) {
                mov(reg_tmp_al, reg_exec_flag);
                and_(reg_tmp_al, FLAG_ZERO_FILTER);
                cmp(reg_tmp_al, 0);
            } else {
                /* none of the other flags are active, so we can use the
                 * register directly */
                cmp(reg_exec_flag, 0);
            }
            je(skip_load_acc);
            load_filter();
            L(skip_load_acc);

        } else {
            load_filter();
        }

        xor_(iter_oh, iter_oh);

        if (handle_padding) {

            /* 'oh loop' initial bounds are stored in bound_table */
            mov(iter_oh_lb, byte[reg_bound_table_addr]);

            /* skip 'oh' row that intersects with top padding */
            xor_(reg_tmp_off, reg_tmp_off);
            mov(reg_tmp_off, iter_oh);
            imul(reg_tmp_off, reg_tmp_off, jcp.ow * ch_offset * sizeof(float));
            add(tmp_reg_idx_output, reg_tmp_off);

            /* forward the input address by 'stride_h' */
            if (jcp.stride_h > 1) {
                xor_(reg_tmp_off, reg_tmp_off);
                mov(reg_tmp_off, iter_oh);
                imul(reg_tmp_off, reg_tmp_off,
                        (jcp.stride_h - 1) * jcp.iw * ch_offset * sizeof(float));
                add(tmp_reg_idx_input, reg_tmp_off);
            }
        }

        L(oh_label);
        {

            compute_ow_step_unroll(l_pad, r_pad, pad_offset, ow_block);

            add(tmp_reg_idx_input,
                    jcp.stride_h * jcp.iw * ch_offset * sizeof(float));
            add(tmp_reg_idx_output, jcp.ow * ch_offset * sizeof(float));

            inc(iter_oh);
            if (handle_padding) {
                /* 'oh loop' end bounds are stored in bound_table (precomputed
                 * during JIT generation) */
                cmp(iter_oh_lb,
                        byte[reg_bound_table_addr + end_bound_table_off]);
            } else {
                cmp(iter_oh, jcp.oh_blk_size);
            }
            jl(oh_label, T_NEAR);
        }

        store_filter();

        add(tmp_reg_filter, jcp.kw * ch_offset * sizeof(float));

        if (handle_padding) {
            xor_(kh_offset, kh_offset);
            mov(kh_offset_lb, byte[reg_bound_table_addr + ih_table_off]);
            /* increase 'ih' row in regards to 'kh'. */
            imul(kh_offset, kh_offset, jcp.iw * ch_offset * sizeof(float));
            add(tmp_reg_kh_input, kh_offset);

            /* increase bound_table idx for the next 'kh' value in table*/
            add(reg_bound_table_addr, sizeof(te_size));
        } else {
            add(tmp_reg_kh_input, jcp.iw * ch_offset * sizeof(float));
        }

        inc(iter_kh);
        cmp(iter_kh, jcp.kh);
        jl(kh_label, T_NEAR);
    }
}

template <cpu_isa_t isa>
inline void
jit_uni_dw_conv_bwd_weights_kernel_f32<isa>::compute_ow_block_unroll() {

    Label skip_load_bias;

    /* Only apply zero_filter (xor'ing accum_reg) on the left edge */
    bool zero_filter_1st_iter = true;

    const int ch_offset = jcp.ch_block;

    const int ow_block_size = jcp.ow_blk_size;
    const int iw_block_size = jcp.ow_blk_size * jcp.stride_w;

    int w_unrolled_loop_count = jcp.ow / ow_block_size;

    const bool handle_padding = (jcp.l_pad > 0) || (jcp.r_pad > 0);

    int pad_offset = jcp.l_pad;

    int ow_block = 0;

    if (jcp.with_bias) {

        zero_bias();

        /* if zero_bias is '1', load bias accumulator from memory. This happens
         * after the first iteration is executed  */
        mov(reg_tmp_al, reg_exec_flag);
        and_(reg_tmp_al, FLAG_ZERO_BIAS);
        cmp(reg_tmp_al, 0);
        je(skip_load_bias);
        load_bias();
        L(skip_load_bias);

        compute_bias_loop();

        store_bias();
    }

    /* compute left padded block */
    if (handle_padding) {

        const int r_pad = jcp.iw - ow_block_size > 0 ? 0 : jcp.r_pad;

        compute_kh_loop(jcp.l_pad, r_pad, 0, zero_filter_1st_iter, ow_block);
        zero_filter_1st_iter = false;

        w_unrolled_loop_count--;

        if (w_unrolled_loop_count >= 1) {
            add(reg_output_baddr, ow_block_size * ch_offset * sizeof(float));
            add(reg_input_baddr, iw_block_size * ch_offset * sizeof(float));
        }
    }

    /* This block may execute under 2 different scenarios:
     * 1) When padding is present, this executes the middle loop (if any).
     * 2) With no padding, it writes the full loop of the micro-kernel. */
    int middle_loop_count = handle_padding ? w_unrolled_loop_count - 1 :
                                             w_unrolled_loop_count;
    if (middle_loop_count >= 1) {
        Label ow_blk_label;

        /* Insert loop for 'ow' block when middle block needs to execute more
         * than once */
        bool do_ow_blk_loop = middle_loop_count > 1;
        if (do_ow_blk_loop) {
            mov(iter_ow_blk, middle_loop_count);
            L(ow_blk_label);
        }

        compute_kh_loop(0, 0, pad_offset, zero_filter_1st_iter);
        /* disable zero_filter for the rest of the iterations i.e. from now on
         * load contents of 'filter' from memory */
        mov(reg_exec_flag, FLAG_ZERO_FILTER);

        if (do_ow_blk_loop || handle_padding) {
            add(reg_output_baddr, ow_block_size * ch_offset * sizeof(float));
            add(reg_input_baddr, iw_block_size * ch_offset * sizeof(float));
        }

        if (do_ow_blk_loop) {
            dec(iter_ow_blk);
            cmp(iter_ow_blk, 0);
            jg(ow_blk_label, T_NEAR);
        }

        w_unrolled_loop_count -= middle_loop_count;
    }

    /* compute right padded block: ow_blk = LAST */
    if (handle_padding && w_unrolled_loop_count >= 1) {
        ow_block = jcp.ow - ow_block_size;
        compute_kh_loop(
                0, jcp.r_pad, pad_offset, zero_filter_1st_iter, ow_block);

        w_unrolled_loop_count--;
    }
}

template <cpu_isa_t isa>
void jit_uni_dw_conv_bwd_weights_kernel_f32<isa>::generate() {
    preamble();

    mov(reg_input_baddr,
            ptr[this->param1 + offsetof(jit_dw_conv_call_s, input)]);
    mov(reg_output_baddr,
            ptr[this->param1 + offsetof(jit_dw_conv_call_s, output)]);
    mov(reg_filter_baddr,
            ptr[this->param1 + offsetof(jit_dw_conv_call_s, filter)]);
    if (jcp.with_bias)
        mov(reg_bias_baddr,
                ptr[this->param1 + offsetof(jit_dw_conv_call_s, bias)]);
    mov(reg_table_flags,
            ptr[this->param1 + offsetof(jit_dw_conv_call_s, table_flags)]);

    compute_ow_block_unroll();

    this->postamble();

    create_h_bounds_table();
}

template <cpu_isa_t isa>
status_t jit_uni_dw_conv_bwd_weights_kernel_f32<isa>::init_conf(
        jit_conv_conf_t &jcp, const convolution_desc_t &cd,
        const memory_desc_wrapper &src_d,
        const memory_desc_wrapper &diff_weights_d,
        const memory_desc_wrapper &diff_dst_d) {

    if (!mayiuse(isa))
        return status::unimplemented;

    jcp.ngroups = diff_weights_d.dims()[0];
    jcp.oc = diff_dst_d.dims()[1] / jcp.ngroups;
    jcp.ic = src_d.dims()[1] / jcp.ngroups;

    const bool with_groups = diff_weights_d.ndims() == src_d.ndims() + 1;

    jcp.is_depthwise = true && with_groups && everyone_is(1, jcp.oc, jcp.ic);

    if (!jcp.is_depthwise)
        return status::unimplemented;

    jcp.ch_block = isa == avx512_common ? 16 : 8;

    jcp.mb = src_d.dims()[0];

    jcp.ih = src_d.dims()[2];
    jcp.iw = src_d.dims()[3];
    jcp.oh = diff_dst_d.dims()[2];
    jcp.ow = diff_dst_d.dims()[3];

    jcp.kh = diff_weights_d.dims()[3];
    jcp.kw = diff_weights_d.dims()[4];

    jcp.stride_h = cd.strides[0];
    jcp.stride_w = cd.strides[1];

    jcp.t_pad = cd.padding[0][0];
    /* bottom padding should equal top padding to generate the proper 'h' loop
     * bounds. */
    jcp.b_pad = cd.padding[1][0];

    jcp.l_pad = cd.padding[0][1];
    jcp.r_pad = cd.padding[1][1];

    jcp.dilate_h = cd.dilates[0];
    jcp.dilate_w = cd.dilates[1];

    jcp.ihp = jcp.ih + jcp.t_pad + jcp.b_pad;
    jcp.iwp = jcp.iw + jcp.l_pad + jcp.r_pad;

    jcp.src_fmt = src_d.format();

    jcp.with_bias = cd.diff_bias_desc.format != memory_format::undef;

    auto desired_act_fmt = isa == avx512_common ? nChw16c : nChw8c;
    auto desired_wei_fmt = isa == avx512_common ? Goihw16g : Goihw8g;

    bool args_ok = true
                   && src_d.format() == desired_act_fmt
                   && diff_weights_d.format() == desired_wei_fmt
                   && diff_dst_d.format() == desired_act_fmt
                   && one_of(cd.bias_desc.format, memory_format::undef, any, x)
                   //&& jcp.ngroups % simd_w == 0
                   && jcp.ngroups % jcp.ch_block == 0
                   && jcp.dilate_h == 0
                   && jcp.dilate_w == 0
                   && jcp.kw <= 3
                   && jcp.oh == (jcp.ihp - jcp.kh) / jcp.stride_h + 1
                   && jcp.ow == (jcp.iwp - jcp.kw) / jcp.stride_w + 1;
    if (!args_ok) return status::unimplemented;

    /* Note: this IMPLICATION-check does not allow 'negative padding' execution
     */
    bool ok = true && IMPLICATION(jcp.r_pad > 0, jcp.r_pad == jcp.l_pad)
            && IMPLICATION(jcp.b_pad > 0, jcp.b_pad == jcp.t_pad);
    if (!ok)
        return status::unimplemented;

    jcp.nb_ch = jcp.ngroups / jcp.ch_block;

    /* Values for block size to try; order gives priority */
    constexpr int BLOCK_SIZE[] = { 14, 16, 7, 8 };

    int block_size_h = 1;
    int block_size_w = 1;

    /* *Try different block sizes for convolution */
    for (int block : BLOCK_SIZE) {

        block_size_h = block / jcp.stride_h;
        block_size_w = block / jcp.stride_w;

        if ((jcp.oh % block_size_h == 0) && (jcp.ow % block_size_w == 0))
            break;
    }

    if (jcp.oh % block_size_h != 0 || jcp.ow % block_size_w != 0)
        return status::unimplemented;

    jcp.oh_blk_size = block_size_h;

    jcp.ur_w = jcp.ow_blk_size = block_size_w;

    return status::success;
}

template struct jit_uni_dw_conv_bwd_weights_kernel_f32<avx512_common>;
template struct jit_uni_dw_conv_bwd_weights_kernel_f32<avx2>;
template struct jit_uni_dw_conv_bwd_weights_kernel_f32<sse42>;

}
}
}
