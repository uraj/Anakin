#include "test/framework/net/debug_info.h"
#include "framework/operators/fusion_ops/conv_batchnorm_scale_relu_pool.h"
#include <assert.h>

std::string g_load_path;

Status read_cmd(int argc, char** argv, std::string& load_path) {
    Status ret = Status();
    if (argc < 2) {
        LOG(FATAL) << "Usage: ./bin load_path";
    }
    if (argc > 1) {
        load_path = std::string(argv[1]);
    }
    return ret;
}

int main(int argc, char** argv) {
    read_cmd(argc, argv, g_load_path);
    std::vector<FuncConf> funcs;
    load_funcs_from_text(g_load_path.c_str(), funcs);
    for (auto& func: funcs) {
        assert(func.type == "ConvBatchnormScaleReluPool");
        auto op_ptr = create_operator<Target, Precision::FP32>(func);
        auto helper_ptr = static_cast<ops::ConvBatchnormScaleReluPool<Target, Precision::FP32>*>(op_ptr)->_helper;
        auto conv_act_pooling_param = \
        static_cast<ops::ConvBatchnormScaleReluPoolHelper<Target, Precision::FP32>*>(helper_ptr) \
        ->_param_conv_batchnorm_scale_relu_pooling;
        LOG(INFO) << "conv_act_pooling_param.conv_param.pad_h: " << conv_act_pooling_param.conv_param.pad_h;
    }
    return 0;
}
