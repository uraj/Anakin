#include "saber/funcs/impl/x86/saber_lstm.h"
#include "sys/time.h"
#include "mkl_cblas.h"
#include "saber/funcs/impl/x86/x86_utils.h"


#ifdef __AVX512F__
#include "saber_avx512_activation.h"
#define SABER_X86_TYPE __m512
#elif __AVX2__
#include "saber_avx2_activation.h"
#define SABER_X86_TYPE __m256
#else
#include "saber_normal_activation.h"
#define SABER_X86_TYPE float
#endif

namespace anakin {

namespace saber {


//inline
static void gemm(const bool TransA, const bool TransB, int m, int n, int k, const float alpha,
                 const float* a, const float* b, const float beta, float* c) {
    //    cout << "(" << m << "," << n << "," << k << ")" << endl;
    int lda = (!TransA/* == CblasNoTrans*/) ? k : m;
    int ldb = (!TransB/* == CblasNoTrans*/) ? n : k;
    CBLAS_TRANSPOSE cuTransA =
        (!TransA/* == CblasNoTrans*/) ? CblasNoTrans : CblasTrans;
    CBLAS_TRANSPOSE cuTransB =
        (!TransB/* == CblasNoTrans*/) ? CblasNoTrans : CblasTrans;
    cblas_sgemm(CblasRowMajor, cuTransA, cuTransB, m, n, k, alpha, a, k, b, n, beta, c, n);
};

template <DataType OpDtype>
SaberStatus SaberLstm<X86, OpDtype>::init(const std::vector<Tensor<X86>*>& inputs,
                                          std::vector<Tensor<X86>*>& outputs,
                                          LstmParam<X86>& param,
                                          Context<X86>& ctx) {
    if(param.with_peephole){
        _hidden_size=param.bias()->valid_size()/7;
    }else{
        _hidden_size=param.bias()->valid_size()/4;
    }
    _word_size=(param.weight()->valid_size()-_hidden_size*_hidden_size*4)/_hidden_size/4;

            DLOG(INFO)<<"wordsize = "<<_word_size;
    int weights_i2h_size=4*_hidden_size*_word_size;
    int weights_h2h_size=4*_hidden_size*_hidden_size;
    int weights_bias_size=4*_hidden_size;
    int weights_peephole_size=3*_hidden_size;

    int aligned_byte= sizeof(SABER_X86_TYPE);
    int c_size=aligned_byte/sizeof(OpDataType);

    _aligned_word_size=utils::round_up(_word_size,c_size);
    _aligned_hidden_size=utils::round_up(_hidden_size,c_size);


    Shape aligned_weights_i2h_shape(1,_word_size,4,_aligned_hidden_size);
    Shape aligned_weights_h2h_shape(1,_aligned_hidden_size,4,_aligned_hidden_size);
    Shape aligned_weights_bias_shape(1,1,4,_aligned_hidden_size);
    utils::try_expand_tensor(_aligned_weights_i2h,aligned_weights_i2h_shape);
    utils::try_expand_tensor(_aligned_weights_h2h,aligned_weights_h2h_shape);
    utils::try_expand_tensor(_aligned_weights_bias,aligned_weights_bias_shape);

    utils::AlignedUtils aligned_tool;
    aligned_tool.aligned_last_dim(param.weight()->data(),_aligned_weights_i2h.mutable_data(),
                                  weights_i2h_size,_hidden_size,_aligned_hidden_size);

    aligned_tool.aligned_last_dim(param.weight()->data() + weights_i2h_size,_aligned_weights_h2h.mutable_data(),
                                  weights_h2h_size,_hidden_size,_aligned_hidden_size);

    aligned_tool.aligned_last_dim(param.bias()->data(),_aligned_weights_bias.mutable_data(),
                                  weights_bias_size,_hidden_size,_aligned_hidden_size);
//FIXME:init weights tensor
    if(param.with_peephole){
        Shape aligned_weights_peephole_shape(1,1,3,_aligned_hidden_size);
        utils:try_expand_tensor(_aligned_weights_peephole,aligned_weights_peephole_shape);
        aligned_tool.aligned_last_dim(param.bias()->data()+weights_bias_size,_aligned_weights_peephole.mutable_data(),
                                      weights_peephole_size,_hidden_size,_aligned_hidden_size);
    }

    return SaberSuccess;
};


template<>
template <typename BIT>
SaberStatus SaberLstm<X86, AK_FLOAT>::
avx_dispatch_with_peephole(const std::vector<Tensor<X86>*>& inputs,
                           std::vector<Tensor<X86>*>& outputs,
                           LstmParam<X86>& param) {

    int loop_div = sizeof(BIT) / sizeof(OpDataType);
    const OpDataType* weight_h = _aligned_weights_h2h.data();
    const OpDataType* weight_w = _aligned_weights_i2h.data();
    const OpDataType* bias = _aligned_weights_bias.data();
    const OpDataType* weight_peephole = _aligned_weights_peephole.data();
    BIT(*gate_act)(const BIT) = act_func[param.gate_activity];
    BIT(*cell_act)(const BIT) = act_func[param.cell_activity];
    BIT(*candi_act)(const BIT) = act_func[param.candidate_activity];

    std::vector<int> offset_vec = inputs[0]->get_seq_offset();
    std::vector<int> length_vec(offset_vec.size() - 1);
    int batch_size = offset_vec.size() - 1;
    int seqsum = 0;
    int max_seq_len = 0;
    bool is_hw2seq = offset_vec.size() > 2;
    int word_sum = is_hw2seq ? offset_vec[offset_vec.size() - 1] : inputs[0]->channel();
    utils::AlignedUtils aligned_utils;
    utils::VectorPrint vector_print;
    const OpDataType* h_init = nullptr;
    const OpDataType* cell_init = nullptr;

    const OpDataType* x = inputs[0]->data();
    OpDataType* out = outputs[0]->mutable_data();
    bool is_reverse = param.is_reverse;

    if (inputs.size() > 1) {
        h_init = inputs[1]->data();
        utils::try_expand_tensor(_aligned_init_hidden,batch_size * _aligned_hidden_size);
        aligned_utils.aligned_last_dim(h_init, (OpDataType*)_aligned_init_hidden.mutable_data(),
                                       batch_size * _hidden_size, _hidden_size, _aligned_hidden_size);
        h_init = _aligned_init_hidden.data();
    } else if (param.init_hidden() != nullptr) {

        h_init =(OpDataType*) param.init_hidden()->data();
        //FIXME:is it correct?
    } else {
        //        _aligned_init_hidden.try_expand_tensor(batch_size * _aligned_hidden_size);
        //        _aligned_init_celll.try_expand_tensor(batch_size * _aligned_hidden_size);
        //        h_init = _aligned_init_hidden.data();
        //        cell_init=_aligned_init_celll.data();
    }

    std::vector<int> emit_offset_vec;
    int emit_length = 0;
    utils::SeqSortedseqTranseUtil transe_util(is_reverse);
    bool transform = transe_util.get_sorted_map(offset_vec, emit_offset_vec, emit_length);

    OpDataType* inner_h_out = out;
    OpDataType* inner_cell = nullptr;
    const OpDataType* inner_x = x;
    const OpDataType* inner_h_init = h_init;

    for (int i = 0; i < offset_vec.size() - 1; ++i) {
        int len = offset_vec[i + 1] - offset_vec[i];
        length_vec[i] = len;
        max_seq_len = max_seq_len > len ? max_seq_len : len;
        seqsum += len;
    }

    utils::try_expand_tensor(_temp_wx,seqsum * 4 * _aligned_hidden_size);
    utils::try_expand_tensor(_temp_wh,batch_size * 4 * _aligned_hidden_size);
    utils::try_expand_tensor(_temp_out,seqsum * _aligned_hidden_size * param.num_direction);
    utils::try_expand_tensor(_temp_cell,batch_size * _aligned_hidden_size);

    if (transform) {
        utils::try_expand_tensor(_temp_x,seqsum * _word_size);
        inner_h_out = _temp_out.mutable_data();
        inner_x = _temp_x.mutable_data();
        transe_util.seq_2_sorted_seq(x, (OpDataType*)inner_x, _word_size);

        if (inner_h_init != nullptr) {
            utils::try_expand_tensor(_temp_h_init,batch_size * _aligned_hidden_size);
            transe_util.hidden_2_sorted_hidden(inner_h_init, (OpDataType*)_temp_h_init.mutable_data(), _aligned_hidden_size);
            inner_h_init = _temp_h_init.data();
        }
    } else if (_hidden_size != _aligned_hidden_size) {
        inner_h_out = _temp_out.mutable_data();
    }

    inner_cell = _temp_cell.mutable_data();
    memset(inner_cell, 0, _temp_cell.valid_size()* sizeof(OpDataType));

    OpDataType* temp_wh = _temp_wh.mutable_data();
    OpDataType* temp_wx = _temp_wx.mutable_data();

    gemm(false, false, seqsum, 4 * _aligned_hidden_size, _word_size, 1.f, inner_x, weight_w, 0.f,
         temp_wx);

    const int i_offset = 0;
    const int f_offset = 1;
    const int c_offset = 2;
    const int o_offset = 3;
    const BIT* b_i = (BIT*)(bias + i_offset * _aligned_hidden_size);
    const BIT* b_f = (BIT*)(bias + f_offset * _aligned_hidden_size);
    const BIT* b_c = (BIT*)(bias + c_offset * _aligned_hidden_size);
    const BIT* b_o = (BIT*)(bias + o_offset * _aligned_hidden_size);

    DLOG(INFO) << "wordsum = " << word_sum << ",emit length = " << emit_offset_vec.size();

    for (int word_id = 0; word_id < emit_length; word_id++) {
        int real_word_id = word_id;
        int last_word_id = word_id - 1;

        if (param.is_reverse && batch_size == 1) {
            real_word_id = emit_length - word_id - 1;
            last_word_id = real_word_id + 1;
        }

        int emit_word_id_start = emit_offset_vec[real_word_id];
        int emit_word_id_end = emit_offset_vec[real_word_id + 1];
        int emit_word_length = emit_word_id_end - emit_word_id_start;
        const float* hin;

        if (word_id == 0 && inner_h_init == nullptr) {
            float* hout = nullptr;
            hout = emit_offset_vec[real_word_id] * _aligned_hidden_size + inner_h_out;

            for (int emit_word_id = emit_word_id_start; emit_word_id < emit_word_id_end; emit_word_id++) {
                int emit_wx_offset = emit_word_id * _aligned_hidden_size * 4;
                const BIT* w_x_i = (BIT*)(temp_wx + i_offset * _aligned_hidden_size + emit_wx_offset);
                const BIT* w_x_f = (BIT*)(temp_wx + f_offset * _aligned_hidden_size + emit_wx_offset);
                const BIT* w_x_c = (BIT*)(temp_wx + c_offset * _aligned_hidden_size + emit_wx_offset);
                const BIT* w_x_o = (BIT*)(temp_wx + o_offset * _aligned_hidden_size + emit_wx_offset);

                const BIT* w_co = (BIT*)(weight_peephole + 2 * _aligned_hidden_size);
                int emit_id_offset = emit_word_id - emit_word_id_start;
                BIT* gate_h_p = (BIT*)(hout + emit_id_offset * _aligned_hidden_size);
                BIT* gate_c_p = (BIT*)(inner_cell + emit_id_offset * _aligned_hidden_size);

                for (int frame_id = 0; frame_id < _aligned_hidden_size / (sizeof(BIT) / sizeof(OpDataType));
                        ++frame_id) {
                    BIT gate_i = gate_act(w_x_i[frame_id] + b_i[frame_id]);
                    BIT gate_c_s = cell_act(w_x_c[frame_id] + b_c[frame_id]);
                    BIT gate_c = gate_i * gate_c_s;
                    BIT gate_o = gate_act(w_x_o[frame_id] + b_o[frame_id] + gate_c * w_co[frame_id]);
                    gate_c_p[frame_id] = gate_c;
                    gate_h_p[frame_id] = gate_o * candi_act(gate_c);
                }
            }

            continue;

        } else if (word_id == 0) {
            hin = inner_h_init;
        } else {
            hin = inner_h_out + emit_offset_vec[last_word_id] * _aligned_hidden_size;
        }

        float* hout = nullptr;
        hout = emit_offset_vec[real_word_id] * _aligned_hidden_size + inner_h_out;

        //wh
        gemm(false, false, emit_word_length, 4 * _aligned_hidden_size, _aligned_hidden_size, 1.0, hin,
             weight_h,
             0.f, temp_wh);

        for (int emit_word_id = emit_word_id_start; emit_word_id < emit_word_id_end; emit_word_id++) {
            int emit_wx_offset = emit_word_id * _aligned_hidden_size * 4;
            const BIT* w_x_i = (BIT*)(temp_wx + i_offset * _aligned_hidden_size + emit_wx_offset);
            const BIT* w_x_f = (BIT*)(temp_wx + f_offset * _aligned_hidden_size + emit_wx_offset);
            const BIT* w_x_c = (BIT*)(temp_wx + c_offset * _aligned_hidden_size + emit_wx_offset);
            const BIT* w_x_o = (BIT*)(temp_wx + o_offset * _aligned_hidden_size + emit_wx_offset);

            int emit_id_offset = emit_word_id - emit_word_id_start;
            int emit_wh_offset = emit_id_offset * _aligned_hidden_size * 4;
            const BIT* w_h_i = (BIT*)(temp_wh + i_offset * _aligned_hidden_size + emit_wh_offset);
            const BIT* w_h_f = (BIT*)(temp_wh + f_offset * _aligned_hidden_size + emit_wh_offset);
            const BIT* w_h_c = (BIT*)(temp_wh + c_offset * _aligned_hidden_size + emit_wh_offset);
            const BIT* w_h_o = (BIT*)(temp_wh + o_offset * _aligned_hidden_size + emit_wh_offset);

            const BIT* w_ci = (BIT*)(weight_peephole + 0 * _aligned_hidden_size);
            const BIT* w_cf = (BIT*)(weight_peephole + 1 * _aligned_hidden_size);
            const BIT* w_co = (BIT*)(weight_peephole + 2 * _aligned_hidden_size);

            BIT* gate_h_p = (BIT*)(hout + emit_id_offset * _aligned_hidden_size);
            BIT* gate_c_p = (BIT*)(inner_cell + emit_id_offset * _aligned_hidden_size);

            for (int frame_id = 0; frame_id < _aligned_hidden_size / (sizeof(BIT) / sizeof(OpDataType));
                    ++frame_id) {
                BIT c_1 = gate_c_p[frame_id];
                BIT gate_i = gate_act(w_x_i[frame_id] + w_h_i[frame_id] + b_i[frame_id] + w_ci[frame_id] * c_1);
                BIT gate_f = gate_act(w_x_f[frame_id] + w_h_f[frame_id] + b_f[frame_id] + w_cf[frame_id] * c_1);
                BIT gate_c_s = cell_act(w_x_c[frame_id] + w_h_c[frame_id] + b_c[frame_id]);
                BIT gate_c = gate_f * c_1 + gate_i * gate_c_s;
                BIT gate_o = gate_act(w_x_o[frame_id] + w_h_o[frame_id] + b_o[frame_id] + gate_c * w_co[frame_id]);
                gate_c_p[frame_id] = gate_c;
                gate_h_p[frame_id] = gate_o * candi_act(gate_c);

            }
        }

    }

    if (transform) {
        transe_util.sorted_seq_2_seq(inner_h_out, out, _hidden_size, _aligned_hidden_size);
    } else if (_hidden_size != _aligned_hidden_size) {
        aligned_utils.unaligned_last_dim((OpDataType*)_temp_out.data(), out, seqsum * _hidden_size, _hidden_size,
                                         _aligned_hidden_size);
    }
    return SaberSuccess;
}

template<>
template <typename BIT>
SaberStatus SaberLstm<X86, AK_FLOAT>::
avx_dispatch_without_peephole(const std::vector<Tensor<X86>*>& inputs,
                              std::vector<Tensor<X86>*>& outputs,
                              LstmParam<X86>& param) {
    int loop_div = sizeof(BIT) / sizeof(OpDataType);
    const OpDataType* weight_h = _aligned_weights_h2h.data();
    const OpDataType* weight_w = _aligned_weights_i2h.data();
    const OpDataType* bias = _aligned_weights_bias.data();
    const OpDataType* weight_peephole = _aligned_weights_peephole.data();
    BIT(*gate_act)(const BIT) = act_func[param.gate_activity];
    BIT(*cell_act)(const BIT) = act_func[param.cell_activity];
    BIT(*candi_act)(const BIT) = act_func[param.candidate_activity];

    std::vector<int> offset_vec = inputs[0]->get_seq_offset();
    std::vector<int> length_vec(offset_vec.size() - 1);
    int batch_size = offset_vec.size() - 1;
    int seqsum = 0;
    int max_seq_len = 0;
    bool is_hw2seq = offset_vec.size() > 2;
    int word_sum = is_hw2seq ? offset_vec[offset_vec.size() - 1] : inputs[0]->channel();
    utils::AlignedUtils aligned_utils;
    utils::VectorPrint vector_print;
    const OpDataType* h_init = nullptr;
    const OpDataType* cell_init = nullptr;

    const OpDataType* x = inputs[0]->data();
    OpDataType* out = outputs[0]->mutable_data();
    bool is_reverse = param.is_reverse;

    if (inputs.size() > 1) {
        h_init = inputs[1]->data();
        utils::try_expand_tensor(_aligned_init_hidden,batch_size * _aligned_hidden_size);
        aligned_utils.aligned_last_dim(h_init,(OpDataType*) _aligned_init_hidden.mutable_data(),
                                       batch_size * _hidden_size, _hidden_size, _aligned_hidden_size);
        h_init = _aligned_init_hidden.data();
    } else if (param.init_hidden() != nullptr) {
        h_init = param.init_hidden()->data();
        //FIXME:is it correct?
    } else {
        //        _aligned_init_hidden.try_expand_tensor(batch_size * _aligned_hidden_size);
        //        _aligned_init_celll.try_expand_tensor(batch_size * _aligned_hidden_size);
        //        h_init = _aligned_init_hidden.data();
        //        cell_init=_aligned_init_celll.data();
    }

    std::vector<int> emit_offset_vec;
    int emit_length = 0;
    utils::SeqSortedseqTranseUtil transe_util(is_reverse);
    bool transform = transe_util.get_sorted_map(offset_vec, emit_offset_vec, emit_length);

    OpDataType* inner_h_out = out;
    OpDataType* inner_cell = nullptr;
    const OpDataType* inner_x = x;
    const OpDataType* inner_h_init = h_init;

    for (int i = 0; i < offset_vec.size() - 1; ++i) {
        int len = offset_vec[i + 1] - offset_vec[i];
        length_vec[i] = len;
        max_seq_len = max_seq_len > len ? max_seq_len : len;
        seqsum += len;
    }

    utils::try_expand_tensor(_temp_wx,seqsum * 4 * _aligned_hidden_size);
    utils::try_expand_tensor(_temp_wh,batch_size * 4 * _aligned_hidden_size);
    utils::try_expand_tensor(_temp_out,seqsum * _aligned_hidden_size * param.num_direction);
    utils::try_expand_tensor(_temp_cell,batch_size * _aligned_hidden_size);

    if (transform) {
        utils::try_expand_tensor(_temp_x,seqsum * _word_size);
        inner_h_out = _temp_out.mutable_data();
        inner_x = _temp_x.mutable_data();
        transe_util.seq_2_sorted_seq(x, (OpDataType*)inner_x, _word_size);

        if (inner_h_init != nullptr) {
            utils::try_expand_tensor(_temp_h_init,batch_size * _aligned_hidden_size);
            transe_util.hidden_2_sorted_hidden(inner_h_init, (OpDataType*)_temp_h_init.mutable_data(), _aligned_hidden_size);
            inner_h_init = _temp_h_init.data();
        }
    } else if (_hidden_size != _aligned_hidden_size) {
        inner_h_out = _temp_out.mutable_data();
    }

    inner_cell = _temp_cell.mutable_data();
    memset(inner_cell, 0, _temp_cell.valid_size()* sizeof(OpDataType));

    OpDataType* temp_wh = _temp_wh.mutable_data();
    OpDataType* temp_wx = _temp_wx.mutable_data();

    gemm(false, false, seqsum, 4 * _aligned_hidden_size, _word_size, 1.f, inner_x, weight_w, 0.f,
         temp_wx);

    const int i_offset = 0;
    const int f_offset = 1;
    const int c_offset = 2;
    const int o_offset = 3;
    const BIT* b_i = (BIT*)(bias + i_offset * _aligned_hidden_size);
    const BIT* b_f = (BIT*)(bias + f_offset * _aligned_hidden_size);
    const BIT* b_c = (BIT*)(bias + c_offset * _aligned_hidden_size);
    const BIT* b_o = (BIT*)(bias + o_offset * _aligned_hidden_size);

    DLOG(INFO) << "wordsum = " << word_sum << ",emit length = " << emit_offset_vec.size();

    for (int word_id = 0; word_id < emit_length; word_id++) {
        int real_word_id = word_id;
        int last_word_id = word_id - 1;

        if (param.is_reverse && batch_size == 1) {
            real_word_id = emit_length - word_id - 1;
            last_word_id = real_word_id + 1;
        }

        int emit_word_id_start = emit_offset_vec[real_word_id];
        int emit_word_id_end = emit_offset_vec[real_word_id + 1];
        int emit_word_length = emit_word_id_end - emit_word_id_start;
        const float* hin;

        if (word_id == 0 && inner_h_init == nullptr) {
            float* hout = nullptr;
            hout = emit_offset_vec[real_word_id] * _aligned_hidden_size + inner_h_out;

            for (int emit_word_id = emit_word_id_start; emit_word_id < emit_word_id_end; emit_word_id++) {
                int emit_wx_offset = emit_word_id * _aligned_hidden_size * 4;
                const BIT* w_x_i = (BIT*)(temp_wx + i_offset * _aligned_hidden_size + emit_wx_offset);
                const BIT* w_x_f = (BIT*)(temp_wx + f_offset * _aligned_hidden_size + emit_wx_offset);
                const BIT* w_x_c = (BIT*)(temp_wx + c_offset * _aligned_hidden_size + emit_wx_offset);
                const BIT* w_x_o = (BIT*)(temp_wx + o_offset * _aligned_hidden_size + emit_wx_offset);

                int emit_id_offset = emit_word_id - emit_word_id_start;
                BIT* gate_h_p = (BIT*)(hout + emit_id_offset * _aligned_hidden_size);
                BIT* gate_c_p = (BIT*)(inner_cell + emit_id_offset * _aligned_hidden_size);

                for (int frame_id = 0; frame_id < _aligned_hidden_size / (sizeof(BIT) / sizeof(OpDataType));
                        ++frame_id) {
                    BIT gate_i = gate_act(w_x_i[frame_id] + b_i[frame_id]);
                    BIT gate_c_s = cell_act(w_x_c[frame_id] + b_c[frame_id]);
                    BIT gate_c = gate_i * gate_c_s;
                    BIT gate_o = gate_act(w_x_o[frame_id] + b_o[frame_id]);
                    gate_c_p[frame_id] = gate_c;
                    gate_h_p[frame_id] = gate_o * candi_act(gate_c);
                }
            }

            continue;

        } else if (word_id == 0) {
            hin = inner_h_init;
        } else {
            hin = inner_h_out + emit_offset_vec[last_word_id] * _aligned_hidden_size;
        }

        float* hout = nullptr;
        hout = emit_offset_vec[real_word_id] * _aligned_hidden_size + inner_h_out;

        //wh
        gemm(false, false, emit_word_length, 4 * _aligned_hidden_size, _aligned_hidden_size, 1.0, hin,
             weight_h,
             0.f, temp_wh);

        for (int emit_word_id = emit_word_id_start; emit_word_id < emit_word_id_end; emit_word_id++) {
            int emit_wx_offset = emit_word_id * _aligned_hidden_size * 4;
            const BIT* w_x_i = (BIT*)(temp_wx + i_offset * _aligned_hidden_size + emit_wx_offset);
            const BIT* w_x_f = (BIT*)(temp_wx + f_offset * _aligned_hidden_size + emit_wx_offset);
            const BIT* w_x_c = (BIT*)(temp_wx + c_offset * _aligned_hidden_size + emit_wx_offset);
            const BIT* w_x_o = (BIT*)(temp_wx + o_offset * _aligned_hidden_size + emit_wx_offset);

            int emit_id_offset = emit_word_id - emit_word_id_start;
            int emit_wh_offset = emit_id_offset * _aligned_hidden_size * 4;
            const BIT* w_h_i = (BIT*)(temp_wh + i_offset * _aligned_hidden_size + emit_wh_offset);
            const BIT* w_h_f = (BIT*)(temp_wh + f_offset * _aligned_hidden_size + emit_wh_offset);
            const BIT* w_h_c = (BIT*)(temp_wh + c_offset * _aligned_hidden_size + emit_wh_offset);
            const BIT* w_h_o = (BIT*)(temp_wh + o_offset * _aligned_hidden_size + emit_wh_offset);

            BIT* gate_h_p = (BIT*)(hout + emit_id_offset * _aligned_hidden_size);
            BIT* gate_c_p = (BIT*)(inner_cell + emit_id_offset * _aligned_hidden_size);

            for (int frame_id = 0; frame_id < _aligned_hidden_size / (sizeof(BIT) / sizeof(OpDataType));
                    ++frame_id) {
                BIT c_1 = gate_c_p[frame_id];
                BIT gate_i = gate_act(w_x_i[frame_id] + w_h_i[frame_id] + b_i[frame_id]);
                BIT gate_f = gate_act(w_x_f[frame_id] + w_h_f[frame_id] + b_f[frame_id]);
                BIT gate_c_s = cell_act(w_x_c[frame_id] + w_h_c[frame_id] + b_c[frame_id]);
                BIT gate_c = gate_f * c_1 + gate_i * gate_c_s;
                BIT gate_o = gate_act(w_x_o[frame_id] + w_h_o[frame_id] + b_o[frame_id]);
                gate_c_p[frame_id] = gate_c;
                gate_h_p[frame_id] = gate_o * candi_act(gate_c);

            }
        }

    }

    if (transform) {
        transe_util.sorted_seq_2_seq(inner_h_out, out, _hidden_size, _aligned_hidden_size);
    } else if (_hidden_size != _aligned_hidden_size) {
        aligned_utils.unaligned_last_dim((OpDataType*)_temp_out.data(), out, seqsum * _hidden_size, _hidden_size,
                                         _aligned_hidden_size);
    }

    return SaberSuccess;
}


template<>
SaberStatus SaberLstm<X86, AK_FLOAT>::
dispatch(const std::vector<Tensor<X86>*>& inputs,
         std::vector<Tensor<X86>*>& outputs,
         LstmParam<X86>& param) {
    CHECK_EQ(inputs.size(), 1) << "only support input size = 1";
    CHECK_EQ(outputs.size(), 1) << "only support outputs size = 1";
    CHECK_EQ(param.init_hidden() == nullptr, true) << "only support param.init_hidden() == nullptr";
    CHECK_EQ(param.num_layers, 1) << "only support param.num_layers==1";

    if (param.with_peephole) {
        avx_dispatch_with_peephole<SABER_X86_TYPE>(inputs, outputs, param);
    } else {
        avx_dispatch_without_peephole<SABER_X86_TYPE>(inputs, outputs, param);
    }

    return SaberSuccess;
}

}
}