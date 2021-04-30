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

#ifndef CPU_WINO_REORDER_HPP
#define CPU_WINO_REORDER_HPP

namespace mkldnn {
namespace impl {
namespace cpu {

template <data_type_t type_i, data_type_t type_o>
struct wino_reorder_t : public cpu_primitive_t {
    struct pd_t : public cpu_reorder_pd_t {
        pd_t(const cpu_memory_pd_t *input_pd, const cpu_memory_pd_t *output_pd,
                const primitive_attr_t *attr)
            : cpu_reorder_pd_t(input_pd, output_pd, attr) {}

        DECLARE_COMMON_PD_T("wino_reorder", wino_reorder_t);

        static status_t create(reorder_pd_t **reorder_pd,
                const memory_pd_t *input_pd, const memory_pd_t *output_pd,
                const primitive_attr_t *attr) {
            assert(input_pd->engine()->kind() == engine_kind::cpu);
            assert(output_pd->engine()->kind() == engine_kind::cpu);
            const memory_desc_wrapper output_d(output_pd);

            bool args_ok = true && input_pd->desc()->data_type == type_i
                    && output_pd->desc()->data_type == type_o
                    && one_of(input_pd->desc()->format, goihw, oihw)
                    && output_pd->desc()->format == wino_fmt
                    && one_of(output_d.wino_desc().wino_format,
                               mkldnn_wino_wei_aaOIoi, mkldnn_wino_wei_aaOio,
                               mkldnn_wino_wei_aaOBiOo,
                               mkldnn_wino_wei_OBaaIBOIio);

            if (!args_ok)
                return status::invalid_arguments;

            auto _pd = new pd_t((const cpu_memory_pd_t *)input_pd,
                    (const cpu_memory_pd_t *)output_pd, attr);
            if (_pd == nullptr)
                return out_of_memory;
            if (_pd->init() != success) {
                delete _pd;
                return unimplemented;
            }
            return safe_ptr_assign<reorder_pd_t>(*reorder_pd, _pd);
        }
    };

private:
    typedef typename prec_traits<type_i>::type in_data_t;
    typedef typename prec_traits<type_o>::type out_data_t;
    const int unsign_val_in_wino_domain_ = 5;

    wino_reorder_t(const pd_t *pd,
            const input_vector &inputs, const output_vector &outputs)
        : cpu_primitive_t(&conf_, inputs, outputs), conf_(*pd) {
        const memory_desc_wrapper input_d(conf_.input_pd());
        const memory_desc_wrapper output_d(conf_.output_pd());

        r_ = output_d.wino_desc().r;
        w_alpha_ = output_d.wino_desc().alpha;
        wino_format_ = output_d.wino_desc().wino_format;

        const auto &in_dims = input_d.dims();
        int groups;
        int groups_offset;
        if (input_d.format() == goihw) {
            groups = in_dims[0];
            groups_offset = 1;
        } else {
            groups = 1;
            groups_offset = 0;
        }
        assert(groups == 1); // groups are not supported now
        MAYBE_UNUSED(groups);

        or_oc_ = in_dims[0 + groups_offset];
        or_ic_ = in_dims[1 + groups_offset];
        kh_ = in_dims[2 + groups_offset];
        kw_ = in_dims[3 + groups_offset];

        oc_ = output_d.wino_desc().oc;
        ic_ = output_d.wino_desc().ic;
        oc_block_ = output_d.wino_desc().oc_block;
        ic_block_ = output_d.wino_desc().ic_block;
        assert(oc_ % oc_block_ == 0 && ic_ % ic_block_ == 0);
        nb_oc_ = oc_ / oc_block_;
        nb_ic_ = ic_ / ic_block_;
        ic2_block_ = 1;
        if (wino_format_ == mkldnn_wino_wei_OBaaIBOIio)
            ic2_block_ = output_d.wino_desc().ic2_block;
        oc2_block_ = output_d.wino_desc().oc2_block;
        assert(nb_ic_ % ic2_block_ == 0 && nb_oc_ % oc2_block_ == 0);

        adj_scale_ = output_d.wino_desc().adj_scale;

        size_wino_wei_ = w_alpha_ * w_alpha_ * oc_ * ic_;
        size_wspace_ = r_ * w_alpha_ * oc_block_;

        wspace_ = (in_data_t *)malloc(sizeof(in_data_t) * size_wspace_, 64);
        tmp_wei_ =
                (out_data_t *)malloc(sizeof(out_data_t) * size_wino_wei_, 64);
    }

    ~wino_reorder_t() {
        free(wspace_);
        free(tmp_wei_);
    }

    void transform(const in_data_t *__restrict input) {
        const memory_desc_wrapper input_d(conf_.input_pd()->desc());

        round_mode_t rmode = conf_.attr()->round_mode_;
        const int smask = conf_.attr()->output_scales_.mask_;
        const int ndims_mask = math::ilog2q(smask + 1);
        const size_t D_mask = utils::array_product(input_d.dims(), ndims_mask);
        const float *__restrict scales = conf_.attr()->output_scales_.scales_;
        assert(D_mask == 1 || D_mask == (size_t)oc_);

        /* transform weights to winograd domain */
        const float G_2x2_3x3[4][3] = { { 1.0, 0.0, 0.0 }, { 0.5, 0.5, 0.5 },
            { 0.5, -0.5, 0.5 }, { 0.0, 0.0, 1.0 } };

        const float G_4x4_3x3[6][3] = { { 1.13777777777778f, 0.f, 0.f },
            { -0.688403361344538f, -0.430252100840336f, -0.26890756302521f },
            { -0.688403361344538f, 0.430252100840336f, -0.26890756302521f },
            { 0.119514472455649f, 0.179271708683473f, 0.26890756302521f },
            { 0.119514472455649f, -0.179271708683473f, 0.26890756302521f },
            { 0.f, 0.f, 1.f } };

        float *__restrict g;
        if (one_of(wino_format_, mkldnn_wino_wei_aaOIoi, mkldnn_wino_wei_aaOio,
            mkldnn_wino_wei_aaOBiOo))
            g = (float *)G_2x2_3x3;
        else if (wino_format_ == mkldnn_wino_wei_OBaaIBOIio)
            g = (float *)G_4x4_3x3;
        else {
            assert("Unknown winograd weights target layout");
            return;
        }

        int Z = oc_ * ic_;
        assert(r_ == kh_ && r_ == kw_);

        for (int iic = 0; iic < ic_; iic++) {
        for (int ob = 0; ob < nb_oc_; ob++) {
            const in_data_t *__restrict _inp
                    = input + (ob * oc_block_ * or_ic_ + iic) * kh_ * kw_;
            out_data_t *__restrict _out
                    = tmp_wei_ + (iic * nb_oc_ + ob) * oc_block_;

            parallel_nd(size_wspace_, [&](int i) { wspace_[i] = 0.f; });

            parallel_nd(r_, w_alpha_, oc_block_,
                [&](int ih, int j, int ioc) {
                for (int iw = 0; iw < r_; ++iw) {
                    int inp_oc = ob * oc_block_ + ioc;
                    int inp_ic = iic;
                    in_data_t inp_v = (inp_ic < or_ic_ && inp_oc < or_oc_)
                        ? _inp[ioc * or_ic_ * kh_ * kw_ + ih * kw_ + iw]
                        : 0.f;
                    wspace_[(ih * w_alpha_ + j) * oc_block_ + ioc]
                            += inp_v * g[j * r_ + iw];
                }
            });

            parallel_nd(w_alpha_, w_alpha_, oc_block_,
                [&](int i, int j, int ioc) {
                float t = 0;
                for (int k = 0; k < r_; ++k)
                    t += g[i * r_ + k]
                            * wspace_[(k * w_alpha_ + j) * oc_block_ + ioc];
                if (type_o == s8) {
                    const float scale = (D_mask == 1)
                        ? scales[0]
                        : scales[ob * oc_block_ + ioc];
                    _out[(i * w_alpha_ + j) * Z + ioc]
                            = qz_b0<in_data_t, out_data_t>()(
                                    (in_data_t)t, scale * adj_scale_, rmode);
                } else {
                    _out[(i * w_alpha_ + j) * Z + ioc] = (out_data_t)t;
                }
            });
        }}
    }

    void reorder_to_aaOIoi(out_data_t *__restrict output) {
        int32_t *__restrict dst_bias = nullptr;
        if (type_o == s8) {
            const auto bias_shift = sizeof(out_data_t) * size_wino_wei_;
            const size_t bias_size = w_alpha_ * w_alpha_ * oc_;

            dst_bias = (int32_t *)(output + bias_shift);
            utils::array_set((int32_t *)dst_bias, 0, bias_size);
        }
        int index = 0;
        for (int u_h = 0; u_h < w_alpha_; u_h++) {
        for (int u_w = 0; u_w < w_alpha_; u_w++) {
            parallel_nd(nb_oc_, oc_block_, [&](int ob, int o) {
                int u_h_shift = u_h * w_alpha_ * ic_ * oc_;
                int u_w_shift = u_w * ic_ * oc_;
                int u_h_shift_b = u_h * w_alpha_ * oc_;
                int u_w_shift_b = u_w * oc_;
                int oc_block_shift = ob * oc_block_ * ic_ + o * ic_block_;
                for (int ib = 0; ib < nb_ic_; ib++) {
                for (int i = 0; i < ic_block_; i++) {
                    int _i = ib * ic_block_;
                    int _o = ob * oc_block_;
                    int ic_shift = (_i + i) * oc_;
                    int oc_shift = (_o + o);
                    int ic_block_shift = ib * oc_block_ * ic_block_ + i;
                    int src_offset =
                            u_h_shift + u_w_shift + ic_shift + oc_shift;
                    int dst_offset = u_h_shift + u_w_shift + oc_block_shift
                            + ic_block_shift;

                    output[dst_offset] = tmp_wei_[src_offset];
                    if (type_o == s8) {
                        int bias_offset = u_h_shift_b + u_w_shift_b + oc_shift;
                        if (index != unsign_val_in_wino_domain_)
                            dst_bias[bias_offset]
                                    -= (128 * (int32_t)output[dst_offset]);
                        else
                            dst_bias[bias_offset] = 0;
                    }
                }}
            });
            index++;
        }}
    }

    void reorder_to_aaOio(out_data_t *__restrict output) {
        parallel_nd(w_alpha_, w_alpha_, nb_oc_,
            [&](int u_h, int u_w, int ob) {
            for (int ib = 0; ib < nb_ic_; ib++) {
            for (int i = 0; i < ic_block_; i++) {
            for (int o = 0; o < oc_block_; o++) {
                int src_offset = u_h * w_alpha_ * ic_ * oc_ + u_w * ic_ * oc_
                    + (ib * ic_block_ + i) * oc_ + (ob * oc_block_ + o);

                int dst_offset
                    = u_h * w_alpha_ * nb_oc_ * nb_ic_ * ic_block_ * oc_block_
                    + u_w * nb_oc_ * nb_ic_ * ic_block_ * oc_block_
                    + ob * nb_ic_ * ic_block_ * oc_block_
                    + ib * ic_block_ * oc_block_ + i * oc_block_ + o;
                output[dst_offset] = tmp_wei_[src_offset];
            }}}
        });
    }

    void reorder_to_aaOBiOo(out_data_t *__restrict output) {
        int oc_chunks = nb_oc_ / oc2_block_;

        parallel_nd(w_alpha_, w_alpha_, oc_chunks,
            [&](int u_h, int u_w, int occ) {
            for (int ib = 0; ib < nb_ic_; ib++) {
                out_data_t *__restrict wei_ptr = output
                    + (((u_h * w_alpha_ + u_w) * oc_chunks + occ) * nb_ic_ + ib)
                    * oc2_block_ * ic_block_ * oc_block_;
                int wei_offset = 0;
                for (int i = 0; i < ic_block_; i++) {
                for (int ob2 = 0; ob2 < oc2_block_; ob2++) {
                    for (int o = 0; o < oc_block_; o++) {
                        int icp = ib * ic_block_ + i;
                        int ocp =
                            occ * oc2_block_ * oc_block_ + ob2 * oc_block_ + o;

                        int src_offset = u_h * w_alpha_ * ic_ * oc_
                            + u_w * ic_ * oc_ + icp * oc_ + ocp;
                        wei_ptr[wei_offset + o] = tmp_wei_[src_offset];
                    }
                    wei_offset += oc_block_;
                }}
            }
        });
    }

    void reorder_to_OBaaIBOIio(out_data_t *__restrict output) {
        int ic_chunks = nb_ic_ / ic2_block_;
        int oc_chunks = nb_oc_ / oc2_block_;

        parallel_nd(oc_chunks, w_alpha_, w_alpha_,
            [&](int occ, int u_h, int u_w) {
            for (int icc = 0; icc < ic_chunks; icc++) {
            for (int ob = 0; ob < oc2_block_; ob++) {
                int ocp = (occ * oc2_block_ + ob) * oc_block_;
                for (int ib = 0; ib < ic2_block_; ib++) {
                for (int i = 0; i < ic_block_; i++) {
                    int icp = (icc * ic2_block_ + ib) * ic_block_ + i;

                    int src_offset = u_h * w_alpha_ * ic_ * oc_
                        + u_w * ic_ * oc_ + icp * oc_ + ocp;
                    int wei_offset
                        = ((((((occ * w_alpha_ + u_h) * w_alpha_ + u_w)
                            * ic_chunks + icc) * oc2_block_ + ob) * ic2_block_
                            + ib) * ic_block_ + i) * oc_block_;
                    for (int o = 0; o < oc_block_; o++)
                        output[wei_offset + o] = tmp_wei_[src_offset + o];
                }}
            }}
        });
    }

    virtual void execute(event_t *e) {
        auto input = reinterpret_cast<const in_data_t *>(input_memory(0));
        auto output = reinterpret_cast<out_data_t *>(memory());

        transform(input);

        /* reorder to winograd domain */
        switch (wino_format_) {
        case mkldnn_wino_wei_aaOIoi: reorder_to_aaOIoi(output); break;
        case mkldnn_wino_wei_aaOio: reorder_to_aaOio(output); break;
        case mkldnn_wino_wei_aaOBiOo: reorder_to_aaOBiOo(output); break;
        case mkldnn_wino_wei_OBaaIBOIio: reorder_to_OBaaIBOIio(output); break;
        default: assert("Unknown wino format"); break;
        }

        e->set_state(event_t::ready);
    }

    pd_t conf_;
    int r_, w_alpha_;
    int ic_, oc_, or_ic_, or_oc_, kh_, kw_;
    int oc_block_, ic_block_, oc2_block_, ic2_block_;
    float adj_scale_;
    int nb_oc_, nb_ic_;
    mkldnn_wino_memory_format_t wino_format_;
    in_data_t *__restrict wspace_;
    out_data_t *__restrict tmp_wei_;
    int size_wino_wei_;
    int size_wspace_;
};

} // namespace cpu
} // namespace impl
} // namespace mkldnn

#endif
