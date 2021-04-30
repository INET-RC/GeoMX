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

#ifndef CPU_SIMPLE_REORDER_HPP
#define CPU_SIMPLE_REORDER_HPP

#include <assert.h>

#include "c_types_map.hpp"
#include "type_helpers.hpp"
#include "math_utils.hpp"
#include "mkldnn_thread.hpp"
#include "utils.hpp"

#include "format_traits.hpp"
#include "cpu_reorder_pd.hpp"
#include "cpu_primitive.hpp"

#include "simple_q10n.hpp"
#include "cpu_isa_traits.hpp"

namespace mkldnn {
namespace impl {
namespace cpu {

using namespace mkldnn::impl::status;
using namespace mkldnn::impl::memory_format;
using namespace mkldnn::impl::data_type;

using dk = data_kind_t;
using bf = block_format_t;

using namespace mkldnn::impl::utils;
using math::saturate;

template<impl::data_type_t type>
using data_t = typename prec_traits<type>::type;

template<impl::data_type_t type_i, impl::data_type_t type_o>
using _qz_a1b0 = qz_a1b0<data_t<type_i>, data_t<type_o>>;

template<impl::data_type_t type_i, impl::data_type_t type_o>
using _qz = qz<data_t<type_i>, data_t<type_o>>;

namespace fmt_order {
    const bool keep = true;
    const bool reverse = false;
    const bool any = keep;
}

namespace spec {
struct direct_copy {};
struct direct_copy_except_dim_0 {};
struct reference {};
}

#define SIMPLE_REORDER_TEMPL_DECL \
    impl::data_type_t type_i, impl::memory_format_t fmt_i, \
    impl::data_type_t type_o, impl::memory_format_t fmt_o, bool order_keep
#define SIMPLE_REORDER_TEMPL_CALL \
    type_i, fmt_i, type_o, fmt_o, order_keep

#define DECLARE_COMMON_PARAMS() \
        const memory_desc_wrapper &input_d = pd->input_pd(); \
        const memory_desc_wrapper &output_d = pd->output_pd(); \
        const float alpha = pd->alpha(); MAYBE_UNUSED(alpha); \
        const float beta = pd->beta(); MAYBE_UNUSED(beta); \
        const round_mode_t rmode = pd->attr()->round_mode_; MAYBE_UNUSED(rmode);

/* specific reorders: common template */
template <SIMPLE_REORDER_TEMPL_DECL, typename spec = void>
struct simple_reorder_impl {};

namespace {
bool simple_fmt_check(bool order_keep, impl::memory_format_t fmt_i,
        impl::memory_format_t fmt_o, const memory_desc_wrapper &input_d,
        const memory_desc_wrapper &output_d) {
    return input_d.format() == (order_keep ? fmt_i : fmt_o)
        && output_d.format() == (order_keep ? fmt_o : fmt_i);
}
bool simple_attr_check(const primitive_attr_t *attr, bool many_scales_support) {
    if (many_scales_support)
        return true;
    return IMPLICATION(attr, attr->output_scales_.mask_ == 0);
}
}

/* specific reorders: implementation */
template <SIMPLE_REORDER_TEMPL_DECL>
struct simple_reorder_impl<SIMPLE_REORDER_TEMPL_CALL,
typename utils::enable_if<fmt_i == any && (false
    || fmt_o == hwio_s8s8
    || fmt_o == hwigo_s8s8)>::type>
{
    static bool is_applicable(const memory_desc_wrapper &input_d,
            const memory_desc_wrapper &output_d, const primitive_attr_t *attr)
    {
        const size_t D_mask = utils::array_product(input_d.dims(),
                                math::ilog2q(attr->output_scales_.mask_ + 1));
        const int oc = (input_d.dims()[fmt_o == hwigo_s8s8 + 0]);
        const int g = (fmt_o == hwigo_s8s8) ? (input_d.dims()[0]) : 1;

        return output_d.format() == fmt_o
            && input_d.data_type() == f32
            && output_d.data_type() == s8
            && (D_mask == 1 || D_mask == (size_t)g * oc);
    }

    static status_t execute(const cpu_reorder_pd_t *pd,
        const data_t<type_i> *input, data_t<type_o> *output) {
        DECLARE_COMMON_PARAMS();

        static constexpr bool w_groups = fmt_o == hwigo_s8s8;

        const auto &dims = input_d.dims();
        const auto &pdims = output_d.blocking_desc().padding_dims;

        const int G = w_groups ? dims[0] : 1;
        const int OC = dims[w_groups + 0];
        const int IC = dims[w_groups + 1];
        const int H = dims[w_groups + 2];
        const int W = dims[w_groups + 3];

        const float *scales = pd->attr()->output_scales_.scales_;
        const size_t D_mask = utils::array_product(input_d.dims(),
                math::ilog2q(pd->attr()->output_scales_.mask_ + 1));

        float adj_scale = (mayiuse(avx512_core_vnni)) ? 1.0f : (1.0f / 2.0f);

        size_t offset = G * pdims[w_groups + 0] * pdims[w_groups + 1] * H * W;
        int32_t *cp = reinterpret_cast<int32_t *>(output + offset);

        parallel_nd(G, OC, [&](int g, int oc) {
            cp[g * OC + oc] = 0;
            for (int ic = 0; ic < IC; ic++)
            for (int h = 0; h < H; h++)
            for (int w = 0; w < W; w++) {
                auto i = input[input_d.blk_off<!w_groups>(g, oc, ic, h, w)];
                auto &o = output[output_d.blk_off<!w_groups>(g, oc, ic, h, w)];
                const float s = scales[(D_mask == 1) ? 0 : g * OC + oc];

                o = qz_b0<data_t<type_i>, data_t<type_o>>()(
                    i, s * adj_scale, rmode);
                cp[g * OC + oc] -= (int32_t)o;
            }
            cp [g * OC + oc] *= 128;
        });
        return success;
    }
};

template <SIMPLE_REORDER_TEMPL_DECL>
struct simple_reorder_impl<SIMPLE_REORDER_TEMPL_CALL,
    typename utils::enable_if<
          (fmt_i == goihw && fmt_o == gOIhw4i16o4i_s8s8)
       || (fmt_i == oihw && fmt_o == OIhw4i16o4i_s8s8)
    >::type>
{
    static bool is_applicable(const memory_desc_wrapper &input_d,
            const memory_desc_wrapper &output_d, const primitive_attr_t *attr)
    {
        const size_t D_mask = utils::array_product(input_d.dims(),
                                math::ilog2q(attr->output_scales_.mask_ + 1));
        const int oc = (input_d.dims()[(fmt_i == goihw) + 0]);
        const int g = (fmt_i == goihw) ? (input_d.dims()[0]) : 1;

        return input_d.format() == fmt_i
            && output_d.format() == fmt_o
            && input_d.data_type() == f32
            && output_d.data_type() == s8
            && (D_mask == 1 || D_mask == (size_t)g * oc);
    }

    static status_t execute(const cpu_reorder_pd_t *pd,
        const data_t<type_i> *input, data_t<type_o> *output) {
        DECLARE_COMMON_PARAMS();

        static constexpr bool w_groups = fmt_i == goihw;
        const int blksize = 16;
        const int sblk = 4;

        const auto &_g_oihw_d = order_keep ? input_d : output_d;
        const auto &dims = input_d.dims();
        const auto &pdims = order_keep
            ? output_d.blocking_desc().padding_dims
            : input_d.blocking_desc().padding_dims;

        const int G = w_groups ? dims[0] : 1;
        const int OC = dims[w_groups + 0];
        const int NB_OC = pdims[w_groups + 0] / blksize;
        const int IC = dims[w_groups + 1];
        const int NB_IC = pdims[w_groups + 1] / blksize;
        const int H = dims[w_groups + 2];
        const int W = dims[w_groups + 3];

        const float *scales = pd->attr()->output_scales_.scales_;
        const size_t D_mask = utils::array_product(input_d.dims(),
                            math::ilog2q(pd->attr()->output_scales_.mask_ + 1));

        float adj_scale = (mayiuse(avx512_core_vnni)) ? 1.f : (1.f / 2.f);

        auto index = [&](const int ic, const int oc) {
            return ((ic / sblk) * blksize * sblk + sblk * oc + ic % sblk);
        };

        auto ker = [&](const data_t<type_i> *inp, data_t<type_o> *out,
            int32_t *c, const float *s, const int oc_block, const int ic_block) {
            for (int ic = 0; ic < ic_block; ++ic) {
            for (int oc = 0; oc < oc_block; ++oc) {
                const auto _g_oihw_off =
                    oc * _g_oihw_d.blocking_desc().strides[0][w_groups + 0]
                  + ic * _g_oihw_d.blocking_desc().strides[0][w_groups + 1];
                out[index(ic, oc)]
                    = qz_b0<data_t<type_i>, data_t<type_o>>()(
                            inp[_g_oihw_off], s[oc] * adj_scale, rmode);
                c[oc] -= (128 * (int32_t)(out[index(ic, oc)]));
            }
            }
        };

        constexpr int i_mult = blksize;
        constexpr int o_mult = 1;

        size_t offset = G * pdims[w_groups+0] * pdims[w_groups+1] * H * W;
        int32_t *cp = reinterpret_cast<int32_t *>(output + offset);
        parallel_nd(G * NB_OC * blksize, [&](int i) {
            cp[i] = 0;
        });

        parallel_nd(G, NB_OC, [&](int g, int O) {
            for (int I = 0; I < NB_IC; I++)
                for (int h = 0; h < H; h++)
                for (int w = 0; w < W; w++) {
                    auto i = &input[input_d.blk_off<!w_groups>(g,
                            i_mult * O, i_mult * I, h, w)];
                    auto o = &output[output_d.blk_off<!w_groups>(
                            g, o_mult * O, o_mult * I, h, w)];
                    const int oc_block = nstl::min(blksize, OC - O * blksize);
                    const int ic_block = nstl::min(blksize, IC - I * blksize);

                    int _offset = (g * NB_OC + O) * blksize;
                    ker(i, o, (order_keep) ? &cp[_offset] : nullptr,
                            &scales[(D_mask == 1) ? 0 : _offset],
                                        oc_block, ic_block);
                }
        });
        return success;
    }
};

template <SIMPLE_REORDER_TEMPL_DECL>
struct simple_reorder_impl<SIMPLE_REORDER_TEMPL_CALL,
    typename utils::enable_if<true
    && format_traits<fmt_i>::blk_fmt == bf::_8i16o2i
    && format_traits<fmt_o>::blk_fmt == bf::_8o16i2o>::type>
{
    static bool is_applicable(const memory_desc_wrapper &input_d,
            const memory_desc_wrapper &output_d, const primitive_attr_t *attr)
    {
        return simple_fmt_check(order_keep, fmt_i, fmt_o, input_d, output_d)
            && simple_attr_check(attr, false);
    }

    static status_t execute(const cpu_reorder_pd_t *pd,
        const data_t<type_i> *input, data_t<type_o> *output) {
        DECLARE_COMMON_PARAMS();

        static constexpr bool w_groups
            = format_traits<fmt_o>::data_kind == dk::gwei;
        constexpr int is_1d = format_traits<fmt_o>::ndims_sp == 1;
        constexpr int is_3d = format_traits<fmt_o>::ndims_sp == 3;
        constexpr int blksize = format_traits<fmt_o>::blk_size;

        const auto &dims = input_d.dims();

        const int G = w_groups ? dims[0] : 1;
        const int NB_OC = dims[w_groups + 0] / blksize;
        const int NB_IC = dims[w_groups + 1] / blksize;
        const int D = is_3d ? dims[w_groups + 2] : 1;
        const int H = is_1d ? 1 : dims[w_groups + 2 + is_3d];
        const int W = dims[w_groups + 3 + is_3d - is_1d];

        auto idx_i = [&](const int oc, const int ic)
        { return ((ic / 2) * blksize * 2 + 2 * oc + ic % 2); };

        auto idx_o = [&](const int oc, const int ic)
        { return ((oc / 2) * blksize * 2 + 2 * ic + oc % 2); };

        auto ker = [&](const data_t<type_i> *i, data_t<type_o> *o) -> void {
            if (alpha == 1.0 && beta == 0.0) {
                for (int ic = 0; ic < blksize; ++ic) {
                    for (int oc = 0; oc < blksize; ++oc) {
                        o[idx_o(oc, ic)] = _qz_a1b0<type_i, type_o>()(
                                i[idx_i(oc, ic)], rmode);
                    }
                }
            } else {
                for (int ic = 0; ic < blksize; ++ic) {
                    for (int oc = 0; oc < blksize; ++oc) {
                        o[idx_o(oc, ic)] = _qz<type_i, type_o>()(
                                i[idx_i(oc, ic)], o[idx_o(oc, ic)], alpha,
                                beta, rmode);
                    }
                }
            }
        };

        parallel_nd(G, NB_OC, NB_IC, D, H, W,
            [&](int g, int o, int i, int d, int h, int w) {
            auto ptr_i = &input[wei_blk_off_like_gwei3D<fmt_i>(
                    input_d, g, o, i, d,  h, w)];
            auto ptr_o = &output[wei_blk_off_like_gwei3D<fmt_o>(
                    output_d, g, o, i, d, h, w)];
            ker(ptr_i, ptr_o);
        });

        return success;
    }
};

/* reorders with tail support */

#define PLAIN_TO_BLOCKED_IS_APPLICABLE() \
    static bool is_applicable(const memory_desc_wrapper &input_d, \
        const memory_desc_wrapper &output_d, const primitive_attr_t *attr) { \
        return simple_attr_check(attr, false) && (order_keep \
                ? output_d.format() == fmt_o && input_d.is_plain() \
                : input_d.format() == fmt_o && output_d.is_plain()); \
    }

template <SIMPLE_REORDER_TEMPL_DECL>
struct simple_reorder_impl<SIMPLE_REORDER_TEMPL_CALL,
typename utils::enable_if<fmt_i == any && (false
    || format_traits<fmt_o>::blk_fmt == bf::_8c
    || format_traits<fmt_o>::blk_fmt == bf::_16c)>::type>
{
    PLAIN_TO_BLOCKED_IS_APPLICABLE();

    static status_t execute(const cpu_reorder_pd_t *pd,
        const data_t<type_i> *input, data_t<type_o> *output) {
        DECLARE_COMMON_PARAMS();

        constexpr int is_1d = format_traits<fmt_o>::ndims_sp == 1;
        constexpr int is_3d = format_traits<fmt_o>::ndims_sp == 3;
        constexpr int blksize = format_traits<fmt_o>::blk_size;

        const auto &flat_d = order_keep ? input_d : output_d;
        const auto &dims = input_d.dims();
        const auto &pdims = order_keep
            ? output_d.blocking_desc().padding_dims
            : input_d.blocking_desc().padding_dims;

        const int C = dims[1];
        const int D = is_3d ? dims[2] : 1;
        const int H = is_1d ? 1 : dims[2 + is_3d];
        const int W = dims[3 + is_3d - is_1d];

        auto ker = [&](const data_t<type_i> *i, data_t<type_o> *o,
            const int c_block) {
            if (alpha == 1.0 && beta == 0.0) {
                for (int w = 0; w < W; ++w)
                for (int c = 0; c < c_block; ++c) {
                    const ptrdiff_t flat_off = 0
                        + c * flat_d.blocking_desc().strides[0][1]
                        + w * flat_d.blocking_desc().strides[0][3 + is_3d
                            - is_1d];
                    if (order_keep) {
                        o[w * blksize + c] = _qz_a1b0<type_i, type_o>()(
                                i[flat_off], rmode);
                    } else {
                        o[flat_off] = _qz_a1b0<type_i, type_o>()(
                                i[w * blksize + c], rmode);
                    }
                }
            } else {
                for (int w = 0; w < W; ++w)
                for (int c = 0; c < c_block; ++c) {
                    const ptrdiff_t flat_off = 0
                        + c * flat_d.blocking_desc().strides[0][1]
                        + w * flat_d.blocking_desc().strides[0][3 + is_3d
                            - is_1d];
                    if (order_keep) {
                        o[w * blksize + c] = _qz<type_i, type_o>()(i[flat_off],
                                o[w * blksize + c], alpha, beta, rmode);
                    } else {
                        o[flat_off] = _qz<type_i, type_o>()(i[w * blksize + c],
                                o[flat_off], alpha, beta, rmode);
                    }
                }
            }
        };

        constexpr int i_c_mult = order_keep ? blksize : 1;
        constexpr int o_c_mult = order_keep ? 1 : blksize;

#       define data_blk_off(md, n, c, d, h) \
        ( is_1d ? (md).blk_off(n, c) \
          : is_3d ? (md).blk_off(n, c, d, h) : (md).blk_off(n, c, h))

        parallel_nd(dims[0], pdims[1] / blksize, D, H,
            [&](int n, int nb_c, int d, int h) {
            auto i = &input[data_blk_off(input_d, n, i_c_mult * nb_c, d, h)];
            auto o = &output[data_blk_off(output_d, n, o_c_mult * nb_c, d, h)];
            const int c_block = nstl::min(blksize, C - nb_c * blksize);
            ker(i, o, c_block);
        });

#       undef data_blk_off

        return success;
    }
};

template <SIMPLE_REORDER_TEMPL_DECL>
struct simple_reorder_impl<SIMPLE_REORDER_TEMPL_CALL,
typename utils::enable_if<fmt_i == any
&& block_format_traits<format_traits<fmt_o>::blk_fmt>::blk_ndims == 2>::type>
{
    PLAIN_TO_BLOCKED_IS_APPLICABLE();

    static status_t execute(const cpu_reorder_pd_t *pd,
        const data_t<type_i> *input, data_t<type_o> *output) {
        DECLARE_COMMON_PARAMS();

        static constexpr bool w_groups
            = format_traits<fmt_o>::data_kind == dk::gwei;
        constexpr int is_1d = format_traits<fmt_o>::ndims_sp == 1;
        constexpr int is_3d = format_traits<fmt_o>::ndims_sp == 3;
        constexpr int blksize = format_traits<fmt_o>::blk_size;

        const auto &flat_d = order_keep ? input_d : output_d;
        const auto &dims = input_d.dims();
        const auto &pdims = order_keep
            ? output_d.blocking_desc().padding_dims
            : input_d.blocking_desc().padding_dims;

        const int G = w_groups ? dims[0] : 1;
        const int OC = dims[w_groups + 0];
        const int NB_OC = pdims[w_groups + 0] / blksize;
        const int IC = dims[w_groups + 1];
        const int NB_IC = pdims[w_groups + 1] / blksize;
        const int D = is_3d ? dims[w_groups + 2] : 1;
        const int H = is_1d ? 1 : dims[w_groups + 2 + is_3d];
        const int W = dims[w_groups + 3 + is_3d - is_1d];

        auto ker = [&](const data_t<type_i> *i, data_t<type_o> *o,
            const int oc_block, const int ic_block) {
#           define blk_off OI_blk_off<format_traits<fmt_o>::blk_fmt>

            if (alpha == 1.0 && beta == 0.0) {
                for (int oc = 0; oc < oc_block; ++oc)
                for (int ic = 0; ic < ic_block; ++ic) {
                    const ptrdiff_t flat_off = 0
                        + oc * flat_d.blocking_desc().strides[0][w_groups + 0]
                        + ic * flat_d.blocking_desc().strides[0][w_groups + 1];
                    if (order_keep) {
                        o[blk_off(oc, ic)] = _qz_a1b0<type_i, type_o>()(
                                i[flat_off], rmode);
                    } else {
                        o[flat_off] = _qz_a1b0<type_i, type_o>()(
                                i[blk_off(oc, ic)], rmode);
                    }
                }
            } else {
                for (int oc = 0; oc < oc_block; ++oc)
                for (int ic = 0; ic < ic_block; ++ic) {
                    const ptrdiff_t flat_off = 0
                        + oc * flat_d.blocking_desc().strides[0][w_groups + 0]
                        + ic * flat_d.blocking_desc().strides[0][w_groups + 1];
                    if (order_keep) {
                        o[blk_off(oc, ic)] = _qz<type_i, type_o>()(i[flat_off],
                                o[blk_off(oc, ic)], alpha, beta, rmode);
                    } else {
                        o[flat_off] = _qz<type_i, type_o>()(i[blk_off(oc, ic)],
                                o[flat_off], alpha, beta, rmode);
                    }
                }
            }

#           undef blk_off
        };


        constexpr int i_mult = order_keep ? blksize : 1;
        constexpr int o_mult = order_keep ? 1 : blksize;

        parallel_nd(G, NB_OC, NB_IC, D, H, W,
            [&](int g, int nb_oc, int nb_ic, int d, int h, int w) {
            auto i = &input[wei_blk_off_like_gwei3D<fmt_o>(input_d,
                    g, i_mult * nb_oc, i_mult * nb_ic, d, h, w)];
            auto o = &output[wei_blk_off_like_gwei3D<fmt_o>(output_d,
                    g, o_mult * nb_oc, o_mult * nb_ic, d, h, w)];
            const int oc_block = nstl::min(blksize, OC - nb_oc * blksize);
            const int ic_block = nstl::min(blksize, IC - nb_ic * blksize);
            ker(i, o, oc_block, ic_block);
        });

        return success;
    }
};

template <SIMPLE_REORDER_TEMPL_DECL>
struct simple_reorder_impl<SIMPLE_REORDER_TEMPL_CALL,
typename utils::enable_if<fmt_i == any && (false
    || format_traits<fmt_o>::blk_fmt == bf::_8o
    || format_traits<fmt_o>::blk_fmt == bf::_16o)>::type>
{
    PLAIN_TO_BLOCKED_IS_APPLICABLE();

    static status_t execute(const cpu_reorder_pd_t *pd,
        const data_t<type_i> *input, data_t<type_o> *output) {
        DECLARE_COMMON_PARAMS();

        static constexpr bool w_groups
            = format_traits<fmt_o>::data_kind == dk::gwei;
        constexpr int is_1d = format_traits<fmt_o>::ndims_sp == 1;
        constexpr int is_3d = format_traits<fmt_o>::ndims_sp == 3;
        constexpr int blksize = format_traits<fmt_o>::blk_size;

        const auto &flat_d = order_keep ? input_d : output_d;
        const auto &dims = input_d.dims();
        const auto &pdims = order_keep
            ? output_d.blocking_desc().padding_dims
            : input_d.blocking_desc().padding_dims;

        const int G = w_groups ? dims[0] : 1;
        const int OC = dims[w_groups + 0];
        const int IC = dims[w_groups + 1];
        const int D = is_3d ? dims[w_groups + 2] : 1;
        const int H = is_1d ? 1 : dims[w_groups + 2 + is_3d];
        const int W = dims[w_groups + 3 + is_3d - is_1d];

        constexpr int i_mult = order_keep ? blksize : 1;
        constexpr int o_mult = order_keep ? 1 : blksize;
        const auto strd_oc = flat_d.blocking_desc().strides[0][w_groups];

        parallel_nd(G, pdims[w_groups + 0] / blksize, IC, D, H, W,
            [&](int g, int nb_oc, int ic, int d, int h, int w) {
            auto i = &input[wei_blk_off_like_gwei3D<fmt_o>(input_d,
                    g, i_mult * nb_oc, ic, d, h, w)];
            auto o = &output[wei_blk_off_like_gwei3D<fmt_o>(output_d,
                    g, o_mult * nb_oc, ic, d, h, w)];
            const int oc_block = nstl::min(blksize, OC - nb_oc * blksize);

            if (alpha == 1.0 && beta == 0.0) {
                for (int oc = 0; oc < oc_block; ++oc) {
                    const auto off = oc * strd_oc;
                    if (order_keep) {
                        o[oc] = _qz_a1b0<type_i, type_o>()(i[off], rmode);
                    } else {
                        o[off] = _qz_a1b0<type_i, type_o>()(i[oc], rmode);
                    }
                }
            } else {
                for (int oc = 0; oc < oc_block; ++oc) {
                    const auto off = oc * strd_oc;
                    if (order_keep) {
                        o[oc] = _qz<type_i, type_o>()(i[off], o[oc], alpha,
                                beta, rmode);
                    } else {
                        o[off] = _qz<type_i, type_o>()(i[oc], o[off], alpha,
                                beta, rmode);
                    }
                }
            }
        });

        return success;
    }
};

/* generic and direct-copy reorders */

template <SIMPLE_REORDER_TEMPL_DECL>
struct simple_reorder_impl<SIMPLE_REORDER_TEMPL_CALL,
    typename utils::enable_if<
        fmt_i == any && fmt_o == any && order_keep == fmt_order::any,
    spec::direct_copy>::type>
{
    static bool is_applicable(const memory_desc_wrapper &input_d,
            const memory_desc_wrapper &output_d, const primitive_attr_t *attr) {
        /* FIXME: is the formula correct? */
        return input_d.similar_to(output_d, true, false, 0)
            && input_d.is_dense() && output_d.is_dense()
            && simple_attr_check(attr, false);
    }

    static status_t execute(const cpu_reorder_pd_t *pd,
        const data_t<type_i> *input, data_t<type_o> *output) {
        DECLARE_COMMON_PARAMS();

        assert(input_d.is_dense());

        input += input_d.blk_off(0);
        output += output_d.blk_off(0);

        const size_t nelems = input_d.nelems();

        constexpr int block_size = 16;
        const auto num_blocks = nelems / block_size;
        const auto rem_elems = nelems % block_size;

        parallel(0, [&](const int ithr, const int nthr) {
            size_t start{0}, end{0};
            balance211(num_blocks, nthr, ithr, start, end);
            start = start * block_size;
            end = end * block_size;

            if (alpha == 1.0 && beta == 0.0) {
                PRAGMA_OMP_SIMD()
                for (size_t e = start; e < end; ++e) {
                    output[e] = qz_a1b0<data_t<type_i>, data_t<type_o>>()
                                (input[e], rmode);
                }
            } else if (alpha == 1.0) {
                PRAGMA_OMP_SIMD()
                for (size_t e = start; e < end; ++e) {
                    output[e] = qz_a1<data_t<type_i>, data_t<type_o>>()
                                (input[e], output[e], beta, rmode);
                }
            } else if (beta == 0.0) {
                PRAGMA_OMP_SIMD()
                for (size_t e = start; e < end; ++e) {
                    output[e] = qz_b0<data_t<type_i>, data_t<type_o>>()
                                (input[e], alpha, rmode);
                }
            } else {
                PRAGMA_OMP_SIMD()
                for (size_t e = start; e < end; ++e) {
                    output[e] = qz<data_t<type_i>, data_t<type_o>>()
                                (input[e], output[e], alpha, beta, rmode);
                }
            }

            if (rem_elems != 0 && ithr == nthr - 1){
                if (alpha == 1.0 && beta == 0.0) {
                    PRAGMA_OMP_SIMD()
                    for (size_t e = nelems - rem_elems; e < nelems; ++e) {
                        output[e] = qz_a1b0<data_t<type_i>,
                            data_t<type_o>>()(input[e], rmode);
                    }
                } else if (alpha == 1.0) {
                    PRAGMA_OMP_SIMD()
                    for (size_t e = nelems - rem_elems; e < nelems; ++e) {
                        output[e] = qz_a1<data_t<type_i>,
                            data_t<type_o>>()(input[e], output[e], beta, rmode);
                    }
                } else if (beta == 0.0) {
                    PRAGMA_OMP_SIMD()
                    for (size_t e = nelems - rem_elems; e < nelems; ++e) {
                        output[e] = qz_b0<data_t<type_i>,
                            data_t<type_o>>()(input[e], alpha, rmode);
                    }
                } else {
                    PRAGMA_OMP_SIMD()
                    for (size_t e = nelems - rem_elems; e < nelems; ++e) {
                        output[e] = qz<data_t<type_i>, data_t<type_o>>()
                                    (input[e], output[e], alpha, beta, rmode);
                   }
               }
            }
        });
        return success;
    }
};

template <SIMPLE_REORDER_TEMPL_DECL>
struct simple_reorder_impl<SIMPLE_REORDER_TEMPL_CALL,
    typename utils::enable_if<
        fmt_i == any && fmt_o == any && order_keep == fmt_order::any,
    spec::direct_copy_except_dim_0>::type>
{
    static bool is_applicable(const memory_desc_wrapper &input_d,
            const memory_desc_wrapper &output_d, const primitive_attr_t *attr) {
        auto is_dense_no_0 = [](const memory_desc_wrapper &data_d) {
            return nelems_no_dim_0(data_d) == _size_no_dim_0(data_d);
        };
        /* FIXME: is the formula correct? */
        return input_d.similar_to(output_d, true, false, 1)
            && is_dense_no_0(input_d) && is_dense_no_0(output_d)
            && simple_attr_check(attr, false);
    }

    static status_t execute(const cpu_reorder_pd_t *pd,
        const data_t<type_i> *input, data_t<type_o> *output) {
        DECLARE_COMMON_PARAMS();

        input += input_d.blk_off(0);
        output += output_d.blk_off(0);

        const int N = input_d.dims()[0];
        const size_t is = input_d.blocking_desc().strides[0][0];
        const size_t os = output_d.blocking_desc().strides[0][0];
        const size_t nelems_no_d0 = nelems_no_dim_0(input_d);
        const size_t work_amount = N * nelems_no_d0;

        if (alpha == 1.0 && beta == 0.0) {
            parallel(0, [&](const int ithr, const int nthr) {
                size_t n{0}, dim1_s{0};
                size_t start{0}, end{0};
                balance211(work_amount, nthr, ithr, start, end);
                nd_iterator_init(start, n, N, dim1_s, nelems_no_d0);
                while(start < end) {
                    size_t work_rem = end - start;
                    size_t dim1_e = dim1_s + work_rem > nelems_no_d0
                        ? nelems_no_d0 : dim1_s + work_rem;
                    PRAGMA_OMP_SIMD()
                    for (size_t e = dim1_s; e < dim1_e; ++e) {
                        output[os * n + e] = _qz_a1b0<type_i, type_o>()(
                                input[is * n + e], rmode);
                    }
                    nd_iterator_jump(start, end, n, N, dim1_s, nelems_no_d0);
                }
            });
        } else {
            parallel(0, [&](const int ithr, const int nthr) {
                size_t n{0}, dim1_s{0};
                size_t start{0}, end{0};
                balance211(work_amount, nthr, ithr, start, end);
                nd_iterator_init(start, n, N, dim1_s, nelems_no_d0);
                while(start < end) {
                    size_t work_rem = end - start;
                    size_t dim1_e =
                        dim1_s + work_rem > nelems_no_d0 ? nelems_no_d0
                        : dim1_s + work_rem;
                    PRAGMA_OMP_SIMD()
                    for (size_t e = dim1_s; e < dim1_e; ++e){
                        output[os * n + e] = _qz<type_i, type_o>()(
                                input[is * n + e], output[os * n + e], alpha,
                                beta, rmode);
                    }
                    nd_iterator_jump(start, end, n, N, dim1_s, nelems_no_d0);
                }
            });
        }

        return success;
    }

private:
    static size_t nelems_no_dim_0(const memory_desc_wrapper &data_d) {
        const int ndims = data_d.ndims();
        if (ndims <= 1) return 1;
        return utils::array_product(data_d.dims() + 1, data_d.ndims() - 1);
    }

    static size_t _size_no_dim_0(const memory_desc_wrapper &data_d) {
        size_t max_size = 0;
        auto &blk = data_d.blocking_desc();
        for (int d = 1; d < data_d.ndims(); ++d) {
            auto block = blk.block_dims[d];
            max_size = nstl::max(max_size,
                    size_t(size_t(blk.padding_dims[d] / block)
                        * blk.strides[0][d]));
            if (block > 1)
                max_size = nstl::max(max_size,
                        size_t(block * blk.strides[1][d]));
        }
        return max_size;
    }
};

template <SIMPLE_REORDER_TEMPL_DECL>
struct simple_reorder_impl<SIMPLE_REORDER_TEMPL_CALL,
    typename utils::enable_if<
        fmt_i == any && fmt_o == any && order_keep == fmt_order::any,
    spec::reference>::type>
{
    static bool is_applicable(const memory_desc_wrapper &input_d,
            const memory_desc_wrapper &output_d, const primitive_attr_t *attr) {
        /* supported smask: 0x0...011..10...0,
         * i.e. 1 should be contiguous */
        int smask = attr ? attr->output_scales_.mask_ : 0;
        for (; smask > 0 && !(smask & 0x1); smask >>= 1);
        for (; smask > 0 && smask & 0x1; smask >>= 1);
        return true
            && input_d.is_blocking_desc()
            && output_d.is_blocking_desc()
            && !output_d.is_additional_buffer()
            && !input_d.is_additional_buffer()
            && smask == 0;
    }

    static status_t execute(const cpu_reorder_pd_t *pd,
        const data_t<type_i> *input, data_t<type_o> *output) {
        DECLARE_COMMON_PARAMS();

        const size_t nelems = input_d.nelems();

        int ndims_start = 0, ndims_mask = 0;
        int smask = pd->attr()->output_scales_.mask_;
        for (; smask > 0 && !(smask & 0x1); smask >>= 1) ++ndims_start;
        for (; smask > 0 && smask & 0x1; smask >>= 1) ++ndims_mask;
        assert(smask == 0);

        const ptrdiff_t D_start
            = utils::array_product(input_d.dims(), ndims_start);
        const ptrdiff_t D_mask
            = utils::array_product(input_d.dims() + ndims_start, ndims_mask);
        const ptrdiff_t D_rest = nelems / D_start / D_mask;

        const float *scales = pd->attr()->output_scales_.scales_;

        parallel_nd(D_start, D_mask, D_rest,
            [&](ptrdiff_t ds, ptrdiff_t dm, ptrdiff_t dr) {
            const float scale = scales[dm];

            const size_t e = (ds * D_mask + dm) * D_rest + dr;
            const auto &i = input[input_d.off_l(e)];
            auto &o = output[output_d.off_l(e)];

            o = _qz<type_i, type_o>()(i, o, scale, beta, rmode);
        });

        return success;
    }
};


/* high level class declaration */

template <SIMPLE_REORDER_TEMPL_DECL, typename spec = void>
struct simple_reorder_t: public cpu_primitive_t {
    struct pd_t: public cpu_reorder_pd_t {
        pd_t(const cpu_memory_pd_t *input_pd, const cpu_memory_pd_t *output_pd,
                const primitive_attr_t *attr)
            : cpu_reorder_pd_t(input_pd, output_pd, attr) {}

        DECLARE_COMMON_PD_T("simple:any", simple_reorder_t);

        static status_t create(reorder_pd_t **reorder_pd,
                const memory_pd_t *input_pd, const memory_pd_t *output_pd,
                const primitive_attr_t *attr) {
            assert(input_pd->engine()->kind() == engine_kind::cpu);
            assert(output_pd->engine()->kind() == engine_kind::cpu);
            bool args_ok = true
                && input_pd->desc()->data_type == type_i
                && output_pd->desc()->data_type == type_o
                && simple_reorder_impl<SIMPLE_REORDER_TEMPL_CALL, spec>::
                is_applicable(input_pd->desc(), output_pd->desc(), attr);
            if (!args_ok)
                return invalid_arguments;

            auto _pd = new pd_t((const cpu_memory_pd_t *)input_pd,
                    (const cpu_memory_pd_t *)output_pd, attr);
            if (_pd == nullptr) return out_of_memory;
            if (_pd->init() != success) { delete _pd; return unimplemented; }
            return safe_ptr_assign<reorder_pd_t>(*reorder_pd, _pd);
        }
    };

    simple_reorder_t(const pd_t *pd, const input_vector &inputs,
            const output_vector &outputs)
        : cpu_primitive_t(&conf_, inputs, outputs), conf_(*pd) {}

    virtual void execute(event_t *e) {
        auto input = reinterpret_cast<const data_t<type_i> *>(
                this->input_memory(0));
        auto output = reinterpret_cast<data_t<type_o> *>(this->memory());
        simple_reorder_impl<SIMPLE_REORDER_TEMPL_CALL, spec>::execute(
                &conf_, input, output);
        e->set_state(event_t::ready);
    }

private:
    pd_t conf_;
};

#undef SIMPLE_REORDER_TEMPL_DECL
#undef SIMPLE_REORDER_TEMPL_CALL

}
}
}

#endif

// vim: et ts=4 sw=4 cindent cino^=l0,\:0,N-s
