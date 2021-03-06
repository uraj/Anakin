#include "saber/funcs/impl/cuda/saber_resize.h"
#include <math.h>

namespace anakin{

namespace saber{

template <typename dtype>
__global__ static void resize_bilinear_custom_kernel(const int wout, const int hout,
                                 const int num,const int channels,
                                 const int dst_stride_w,
                                 const int dst_stride_h,
                                 const int dst_stride_c,
                                 const int dst_stride_batch,
                                 const int win, const int hin,
                                 const int src_stride_w,
                                 const int src_stride_h,
                                 const int src_stride_c,
                                 const int src_stride_batch,
                                 const float scale_w, const float scale_h,
                                 const dtype* src, dtype* dst)
{

    int dst_w = blockIdx.x * blockDim.x + threadIdx.x;
    int dst_h = blockIdx.y * blockDim.y + threadIdx.y;

    if (dst_w < wout && dst_h < hout){
#if 0 //! more precise method
        float fw = scale_w * (dst_w + 0.5f) - 0.5f;
        float fh = scale_h * (dst_h + 0.5f) - 0.5f;
        int src_w = int(floor(fw));
        int w = src_w + 1;
        int src_h = int(floor(fh));
        int h = src_h + 1;
#else
        float fh = scale_h * dst_h;
        float fw = scale_w * dst_w;
        const int src_h = int(fh);
        const int src_w = int(fw);
        int w = src_w + 1;
        int h = src_h + 1;
#endif
        fh -= src_h;
        fw -= src_w;
        const float w_h0 = 1.0f - fh;
        const float w_w0 = 1.0f - fw;
        const float w_h1 = fh;
        const float w_w1 = fw;

        float w_00 = w_h0 * w_w0;
        float w_01 = w_h0 * w_w1;
        float w_10 = w_h1 * w_w0;
        float w_11 = w_h1 * w_w1;

        for (int i = 0; i < num; ++i) {
            int src_batch_idx = i * src_stride_batch;

            int hl = src_h * src_stride_h;
            int hh = h * src_stride_h;
            int wl = src_w * src_stride_w;
            int wh = w * src_stride_w;

            int src_indexTL = src_batch_idx + hl + wl;
            int src_indexTR = src_batch_idx + hl + wh;
            int src_indexBL = src_batch_idx + hh + wl;
            int src_indexBR = src_batch_idx + hh + wh;

            int dst_index = i * dst_stride_batch + dst_w * dst_stride_w + dst_h * dst_stride_h;

            for (int j = 0; j < channels; ++j) {
#if 0
                dtype tl = (src_w < 0 || src_h < 0)? 0 : src[src_indexTL];
                dtype tr = (w > win || src_h < 0)? 0 : src[src_indexTR];
                dtype bl = (src_w < 0 || h > hin)? 0 : src[src_indexBL];
                dtype br = (w > win || h > hin)? 0 : src[src_indexBR];
#else
                dtype tl = src[src_indexTL];
                dtype tr = w >= win? 0 : src[src_indexTR];//w > win? 0 :
                dtype bl = h >= hin? 0 : src[src_indexBL];//h > hin? 0 :
                dtype br = (w >= win || h >= hin)? 0 : src[src_indexBR];//(w > win || h > hin)? 0 :
#endif
                dst[dst_index] = static_cast<dtype>(w_00 * tl + w_01 * tr + w_10 * bl + w_11 * br);
                src_indexBR += src_stride_c;
                src_indexBL += src_stride_c;
                src_indexTR += src_stride_c;
                src_indexTL += src_stride_c;
                dst_index += dst_stride_c;
            }
        }
    }
}

template <typename dtype>
__global__ static void resize_bilinear_no_align_kernel(const int wout, const int hout,
                                 const int num,const int channels,
                                 const int dst_stride_w,
                                 const int dst_stride_h,
                                 const int dst_stride_c,
                                 const int dst_stride_batch,
                                 const int win, const int hin,
                                 const int src_stride_w,
                                 const int src_stride_h,
                                 const int src_stride_c,
                                 const int src_stride_batch,
                                 const float scale_w, const float scale_h,
                                 const dtype* src, dtype* dst)
{

    int dst_w = blockIdx.x * blockDim.x + threadIdx.x;
    int dst_h = blockIdx.y * blockDim.y + threadIdx.y;

    if (dst_w < wout && dst_h < hout){
        float scale_w_new = (float)win / wout;
        float scale_h_new = (float)hin / hout;
        float fh = scale_h_new * (dst_h + 0.5) - 0.5;
        float fw = scale_w_new * (dst_w + 0.5) - 0.5;
        fh = fh < 0 ? 0 : fh;
        fw = fw < 0 ? 0 : fw;
        const int src_h = int(fh);
        const int src_w = int(fw);
        int w_id = src_w < win - 1 ? 1 : 0;
        int h_id = src_h < hin -1 ? 1 : 0;
        int w = src_w + w_id;
        int h = src_h + h_id;

        fh -= src_h;
        fw -= src_w;
        const float w_h0 = 1.0f - fh;
        const float w_w0 = 1.0f - fw;
        const float w_h1 = fh;
        const float w_w1 = fw;

        float w_00 = w_h0 * w_w0;
        float w_01 = w_h0 * w_w1;
        float w_10 = w_h1 * w_w0;
        float w_11 = w_h1 * w_w1;

        for (int i = 0; i < num; ++i) {
            int src_batch_idx = i * src_stride_batch;

            int hl = src_h * src_stride_h;
            int hh = h * src_stride_h;
            int wl = src_w * src_stride_w;
            int wh = w * src_stride_w;

            int src_indexTL = src_batch_idx + hl + wl;
            int src_indexTR = src_batch_idx + hl + wh;
            int src_indexBL = src_batch_idx + hh + wl;
            int src_indexBR = src_batch_idx + hh + wh;

            int dst_index = i * dst_stride_batch + dst_w * dst_stride_w + dst_h * dst_stride_h;

            for (int j = 0; j < channels; ++j) {
                dtype tl = src[src_indexTL];
                dtype tr = src[src_indexTR];//w > win? 0 :
                dtype bl = src[src_indexBL];//h > hin? 0 :
                dtype br = src[src_indexBR];//(w > win || h > hin)? 0 :

                dst[dst_index] = static_cast<dtype>(w_00 * tl + w_01 * tr + w_10 * bl + w_11 * br);
                src_indexBR += src_stride_c;
                src_indexBL += src_stride_c;
                src_indexTR += src_stride_c;
                src_indexTL += src_stride_c;
                dst_index += dst_stride_c;
            }
        }
    }
}

template <typename dtype>
__global__ static void resize_bilinear_align_kernel(const int wout, const int hout,
                                 const int num,const int channels,
                                 const int dst_stride_w,
                                 const int dst_stride_h,
                                 const int dst_stride_c,
                                 const int dst_stride_batch,
                                 const int win, const int hin,
                                 const int src_stride_w,
                                 const int src_stride_h,
                                 const int src_stride_c,
                                 const int src_stride_batch,
                                 const float scale_w, const float scale_h,
                                 const dtype* src, dtype* dst)
{

    int dst_w = blockIdx.x * blockDim.x + threadIdx.x;
    int dst_h = blockIdx.y * blockDim.y + threadIdx.y;

    if (dst_w < wout && dst_h < hout){

        float scale_w_new = (float)(win - 1) / (wout - 1);
        float scale_h_new = (float)(hin - 1) / (hout - 1);
        float fh = scale_h_new * dst_h;
        float fw = scale_w_new * dst_w;
        const int src_h = int(fh);
        const int src_w = int(fw);
        int w_id = src_w < win - 1 ? 1 : 0;
        int h_id = src_h < hin -1 ? 1 : 0;
        int w = src_w + w_id;
        int h = src_h + h_id;
        fh -= src_h;
        fw -= src_w;
        const float w_h0 = 1.0f - fh;
        const float w_w0 = 1.0f - fw;
        const float w_h1 = fh;
        const float w_w1 = fw;

        float w_00 = w_h0 * w_w0;
        float w_01 = w_h0 * w_w1;
        float w_10 = w_h1 * w_w0;
        float w_11 = w_h1 * w_w1;

        for (int i = 0; i < num; ++i) {
            int src_batch_idx = i * src_stride_batch;

            int hl = src_h * src_stride_h;
            int hh = h * src_stride_h;
            int wl = src_w * src_stride_w;
            int wh = w * src_stride_w;

            int src_indexTL = src_batch_idx + hl + wl;
            int src_indexTR = src_batch_idx + hl + wh;
            int src_indexBL = src_batch_idx + hh + wl;
            int src_indexBR = src_batch_idx + hh + wh;

            int dst_index = i * dst_stride_batch + dst_w * dst_stride_w + dst_h * dst_stride_h;

            for (int j = 0; j < channels; ++j) {
                dtype tl = src[src_indexTL];
                dtype tr = src[src_indexTR];//w > win? 0 :
                dtype bl = src[src_indexBL];//h > hin? 0 :
                dtype br = src[src_indexBR];//(w > win || h > hin)? 0 :

                dst[dst_index] = static_cast<dtype>(w_00 * tl + w_01 * tr + w_10 * bl + w_11 * br);
                src_indexBR += src_stride_c;
                src_indexBL += src_stride_c;
                src_indexTR += src_stride_c;
                src_indexTL += src_stride_c;
                dst_index += dst_stride_c;
            }
        }
    }
}

template <typename dtype, bool align>
__global__ static void resize_nearest_kernel(const int wout, const int hout,
                                 const int num,const int channels,
                                 const int dst_stride_w,
                                 const int dst_stride_h,
                                 const int dst_stride_c,
                                 const int dst_stride_batch,
                                 const int win, const int hin,
                                 const int src_stride_w,
                                 const int src_stride_h,
                                 const int src_stride_c,
                                 const int src_stride_batch,
                                 const float scale_w, const float scale_h,
                                 const dtype* src, dtype* dst)
{

    int dst_w = blockIdx.x * blockDim.x + threadIdx.x;
    int dst_h = blockIdx.y * blockDim.y + threadIdx.y;

    if (dst_w < wout && dst_h < hout){

        float scale_w_new = (float)(win - 1) / (wout - 1);
        float scale_h_new = (float)(hin - 1) / (hout - 1);

        int fh = static_cast<int>(scale_h_new * dst_h + 0.5);
        int fw = static_cast<int>(scale_w_new * dst_w + 0.5);
        fh = fh < 0 ? 0 : fh;
        fw = fw < 0 ? 0 : fw;
        const int src_h = fh;
        const int src_w = fw;

        for (int i = 0; i < num; ++i) {
            int src_index = i * src_stride_batch + src_h * src_stride_h + src_w * src_stride_w;
            int dst_index = i * dst_stride_batch + dst_w * dst_stride_w + dst_h * dst_stride_h;

            for (int j = 0; j < channels; ++j) {

                dst[dst_index] = src[src_index];
                src_index += src_stride_c;
                dst_index += dst_stride_c;
            }
        }
    }
}


template <DataType OpDtype>
SaberStatus SaberResize<NV, OpDtype>::dispatch(\
    const std::vector<Tensor<NV> *>& inputs, \
    std::vector<Tensor<NV> *>& outputs, \
    ResizeParam<NV>& param) {

    CHECK_EQ(inputs[0]->get_dtype() == OpDtype && outputs[0]->get_dtype() == OpDtype, true) << \
    "input datatype, output datatype are not match to Op datatype";
    cudaStream_t stream = this->_ctx->get_compute_stream();

    int w_out = outputs[0]->width();
    int h_out = outputs[0]->height();
    int c_out = outputs[0]->channel();
    int n_out = outputs[0]->num();

    if (inputs.size() > 1) {
        int* out_size_data = static_cast<int*>(inputs[1]->data());
        h_out = out_size_data[0];
        w_out = out_size_data[1];
        outputs[0]->reshape(Shape({n_out, c_out, h_out, w_out}));
    }

    int w_in = inputs[0]->width();
    int h_in = inputs[0]->height();
    int c_in = inputs[0]->channel();
    int n_in = inputs[0]->num();

    int num_idx = inputs[0]->num_index();
    int channel_idx = inputs[0]->channel_index();
    int height_idx = inputs[0]->height_index();
    int width_idx = inputs[0]->width_index();

    int dims = inputs[0]->dims();

    CHECK_EQ(c_in, c_out) << "input channel should = output channel";
    CHECK_EQ(c_in, c_out) << "input batch size should = output batch size";

    int block_x = 8;
    int block_y = 8;
    int grid_x = (w_out + block_x - 1) / block_x;
    int grid_y = (h_out + block_y - 1) / block_y;
    dim3 block(block_x, block_y);
    dim3 grid(grid_x, grid_y);

    Shape src_real_shape;
    Shape dst_real_shape;
    if (inputs[0]->is_continue_mem()) {
        src_real_shape = inputs[0]->valid_shape();
    } else {
        src_real_shape = inputs[0]->shape();
    }
    if (outputs[0]->is_continue_mem()) {
        dst_real_shape = outputs[0]->valid_shape();
    } else {
        dst_real_shape = outputs[0]->shape();
    }
    float scale_w = 0.f;
    float scale_h = 0.f;
    if (param.out_width != -1 && param.out_height != -1){
        scale_w = (float)param.out_width / w_in;
        scale_h = (float)param.out_height / h_in;
    } else {
        scale_w = param.width_scale;
        scale_h = param.height_scale;
    }
    int src_stride_w = src_real_shape.count(width_idx + 1);//inputs[0]->count_valid(width_idx + 1, dims);
    int src_stride_h = src_real_shape.count(height_idx + 1);//inputs[0]->count_valid(height_idx + 1, dims);
    int src_stride_channel = src_real_shape.count(channel_idx + 1);//inputs[0]->count_valid(channel_idx + 1, dims);
    int src_stride_batch = src_real_shape.count(num_idx + 1);//inputs[0]->count_valid(num_idx + 1, dims);
    int dst_stride_w = dst_real_shape.count(width_idx + 1);//outputs[0]->count(width_idx + 1, dims);
    int dst_stride_h = dst_real_shape.count(height_idx + 1);//outputs[0]->count(height_idx + 1, dims);
    int dst_stride_channel = dst_real_shape.count(channel_idx + 1);//outputs[0]->count(channel_idx + 1, dims);
    int dst_stride_batch = dst_real_shape.count(num_idx + 1);//outputs[0]->count(num_idx + 1, dims);
    switch (param.resize_type){
        case BILINEAR_ALIGN:
            resize_bilinear_align_kernel<OpDataType><<<grid, block, 0, stream>>>(w_out, h_out, n_out, c_out, \
                        dst_stride_w, dst_stride_h, dst_stride_channel, dst_stride_batch, \
                        w_in, h_in, src_stride_w, src_stride_h, src_stride_channel, src_stride_batch, \
                        1 / scale_w, 1 / scale_h, \
                        (const OpDataType*)inputs[0]->data(), (OpDataType*)outputs[0]->mutable_data());;
            break;
        case BILINEAR_NO_ALIGN:
            resize_bilinear_no_align_kernel<OpDataType><<<grid, block, 0, stream>>>(w_out, h_out, n_out, c_out, \
                        dst_stride_w, dst_stride_h, dst_stride_channel, dst_stride_batch, \
                        w_in, h_in, src_stride_w, src_stride_h, src_stride_channel, src_stride_batch, \
                        1 / scale_w, 1 / scale_h, \
                        (const OpDataType*)inputs[0]->data(), (OpDataType*)outputs[0]->mutable_data());;
            break;
        case RESIZE_CUSTOM:
            resize_bilinear_custom_kernel<OpDataType><<<grid, block, 0, stream>>>(w_out, h_out, n_out, c_out, \
                        dst_stride_w, dst_stride_h, dst_stride_channel, dst_stride_batch, \
                        w_in, h_in, src_stride_w, src_stride_h, src_stride_channel, src_stride_batch, \
                        1 / scale_w, 1 / scale_h, \
                        (const OpDataType*)inputs[0]->data(), (OpDataType*)outputs[0]->mutable_data());;
            break;
        case NEAREST_ALIGN:
            resize_nearest_kernel<OpDataType, true><<<grid, block, 0, stream>>>(w_out, h_out, n_out, c_out, \
                        dst_stride_w, dst_stride_h, dst_stride_channel, dst_stride_batch, \
                        w_in, h_in, src_stride_w, src_stride_h, src_stride_channel, src_stride_batch, \
                        1 / scale_w, 1 / scale_h, \
                        (const OpDataType*)inputs[0]->data(), (OpDataType*)outputs[0]->mutable_data());;
            break;
        default:
            LOG(FATAL) << "Unimply resize type: " << (int)param.resize_type;
    }

    //outputs[0]->record_event(stream);
    return SaberSuccess;
}
template class SaberResize<NV, AK_FLOAT>;
template class SaberResize<NV, AK_INT8>;
DEFINE_OP_TEMPLATE(SaberResize, ResizeParam, NV, AK_HALF);
} //namespace anakin

} //namespace
