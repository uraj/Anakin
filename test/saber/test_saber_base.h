/* Copyright (c) 2018 Anakin Authors, Inc. All Rights Reserved.
   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at
       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#ifndef ANAKIN_TEST_SABER_BASE_H
#define ANAKIN_TEST_SABER_BASE_H

#include "saber/funcs/base.h"
#include "saber/core/tensor.h"
#include "saber/core/shape.h"
#include "saber/saber_types.h"
#include "saber/core/tensor_op.h"
#include "test/saber/test_saber_func.h"
#include "saber/core/data_traits.h"
#include "utils/unit_test/aktest.h"
#include "utils/logger/logger.h"
#include "saber/funcs/debug.h"
#include <vector>
#include <string>

using namespace anakin :: test;
namespace anakin {
namespace saber {
template <typename TargetType_D, typename TargetType_H, DataType Dtype,
          template <typename T, DataType D> class Op,
          template <typename T> class Param>
class TestSaberBase {
public:
    typedef Param<TargetType_D> Param_t;
    typedef Op<TargetType_D, Dtype> Op_t;
    typedef Tensor<TargetType_H> TensorH;
    typedef Tensor<TargetType_D> TensorD;
    typedef std::vector<TensorD*> Input_dt;
    typedef std::vector<TensorD*> Output_dt;
    typedef std::vector<TensorH*> Input_ht;
    typedef std::vector<TensorH*> Output_ht;
    typedef typename DataTrait<TargetType_H, Dtype>::Dtype OpDataType;
    typedef void (*CpuFunc_t)(const Input_ht&, Output_ht&, Param_t& param);

    TestSaberBase(int in_num = 1, int out_num = 1) : _op_input_num(in_num),  _op_output_num(out_num) {
        Env<TargetType_D> :: env_init();
        Env<TargetType_H> :: env_init();
    }
    ~TestSaberBase() {
        clear_datas();
    }
    void add_param(Param_t& param) {
        _params.push_back(param);
    }
    void set_param(Param_t& param) {
        _params.clear();
        _params.push_back(param);
    }

    void add_inputs_shape(Shape new_shape, std::vector<float> in_tensor_scale = {},
                          std::vector<float> out_tensor_scale = {}, LayoutType output_layout = Layout_NCHW) {

        std :: vector<TensorD*> in_d;
        std :: vector<TensorH*> in_h;
        std :: vector<TensorD*> out_d;
        std :: vector<TensorH*> out_h;
        std :: vector<TensorH*> out_hd;

        for (int i = 0; i < _op_input_num; ++i) {
            TensorD* d_id = new TensorD(new_shape, _in_data_type);
            TensorH* d_ih = new TensorH(new_shape, _in_data_type);
            d_id->set_scale(in_tensor_scale);
            d_ih->set_scale(in_tensor_scale);
            in_d.push_back(d_id);
            in_h.push_back(d_ih);
        }

        for (int i = 0; i < _op_output_num; ++i) {
            TensorD* d_od = new TensorD(Shape({1, 1, 1, 1}, output_layout));
            TensorH* d_oh = new TensorH(Shape({1, 1, 1, 1}, output_layout));
            TensorH* d_ohd = new TensorH(Shape({1, 1, 1, 1}, output_layout));
            d_od->set_scale(out_tensor_scale);
            d_oh->set_scale(out_tensor_scale);
            d_ohd->set_scale(out_tensor_scale);
            out_d.push_back(d_od);
            out_h.push_back(d_oh);
            out_hd.push_back(d_ohd);
        }

        clear_datas();
        _inputs_dev.push_back(in_d);
        _inputs_host.push_back(in_h);
        _outputs_dev.push_back(out_d);
        _outputs_host.push_back(out_h);
        _outputs_hd.push_back(out_hd);
        _input_shapes.push_back(std::vector<Shape> {new_shape});


    }

    void add_inputs_shape(std::vector<Shape> new_shape_v) {

        CHECK_GE(new_shape_v.size(), _op_input_num) << "unvaliable shape vector";

        std :: vector<TensorD*> in_d;
        std :: vector<TensorH*> in_h;
        std :: vector<TensorD*> out_d;
        std :: vector<TensorH*> out_h;
        std :: vector<TensorH*> out_hd;

        for (int i = 0; i < _op_input_num; ++i) {
            TensorD* d_id = new TensorD(new_shape_v[i], _in_data_type);
            TensorH* d_ih = new TensorH(new_shape_v[i], _in_data_type);
            in_d.push_back(d_id);
            in_h.push_back(d_ih);

        }

        for (int i = 0; i < _op_output_num; ++i) {
            TensorD* d_od = new TensorD();
            TensorH* d_oh = new TensorH();
            TensorH* d_ohd = new TensorH();
            out_d.push_back(d_od);
            out_h.push_back(d_oh);
            out_hd.push_back(d_ohd);
        }

        clear_datas();

        _inputs_dev.push_back(in_d);
        _inputs_host.push_back(in_h);
        _outputs_dev.push_back(out_d);
        _outputs_host.push_back(out_h);
        _outputs_hd.push_back(out_hd);
        _input_shapes.push_back(new_shape_v);
        _input_type = RANDOM;
    }

    void add_inputs_shape(std::vector<Shape> new_shape_v,
                          std::vector<std::vector<float>> in_tensor_scale,
                          std::vector<std::vector<float>> out_tensor_scale) {

        CHECK_GE(new_shape_v.size(), _op_input_num) << "unvaliable shape vector";
        CHECK_EQ(in_tensor_scale.size(), new_shape_v.size());
        CHECK_EQ(out_tensor_scale.size(), new_shape_v.size());
        std :: vector<TensorD*> in_d;
        std :: vector<TensorH*> in_h;
        std :: vector<TensorD*> out_d;
        std :: vector<TensorH*> out_h;
        std :: vector<TensorH*> out_hd;

        for (int i = 0; i < _op_input_num; ++i) {
            TensorD* d_id = new TensorD(new_shape_v[i], _in_data_type);
            TensorH* d_ih = new TensorH(new_shape_v[i], _in_data_type);
            d_id->set_scale(in_tensor_scale[i]);
            d_ih->set_scale(in_tensor_scale[i]);
            in_d.push_back(d_id);
            in_h.push_back(d_ih);

        }

        for (int i = 0; i < _op_output_num; ++i) {
            TensorD* d_od = new TensorD();
            TensorH* d_oh = new TensorH();
            TensorH* d_ohd = new TensorH();
            d_od->set_scale(out_tensor_scale[i]);
            d_oh->set_scale(out_tensor_scale[i]);
            d_ohd->set_scale(out_tensor_scale[i]);
            out_d.push_back(d_od);
            out_h.push_back(d_oh);
            out_hd.push_back(d_ohd);
        }

        clear_datas();

        _inputs_dev.push_back(in_d);
        _inputs_host.push_back(in_h);
        _outputs_dev.push_back(out_d);
        _outputs_host.push_back(out_h);
        _outputs_hd.push_back(out_hd);
        _input_shapes.push_back(new_shape_v);
        _input_type = RANDOM;
    }

    void set_input_shape(Shape new_shape, std::vector<float> scale_in, std::vector<float> scale_out,
                         TestDataType type = RANDOM, OpDataType value = 1, LayoutType output_layout = Layout_NCHW) {
        //clear_datas();

        add_inputs_shape(new_shape, scale_in, scale_out, output_layout);
        _input_type = type;
        _special_value = value;
    }

    void set_input_shape(Shape new_shape, TestDataType type = RANDOM, OpDataType value = 1,
                         LayoutType output_layout = Layout_NCHW) {
        //clear_datas();

        add_inputs_shape(new_shape, {}, {}, output_layout);
        _input_type = type;
        _special_value = value;
    }

    void set_input_shape(std::vector<Shape> new_shape_v, TestDataType type = RANDOM,
                         OpDataType value = 1) {
        //clear_datas();

        add_inputs_shape(new_shape_v);
        _input_type = type;
        _special_value = value;
    }
    void auto_gen_inputs() {
        CHECK_EQ(_op_input_num, 1) << "only support input_num == 1";

        for (int n : {
                    1, 2
                }) {
            for (int c : {
                        32, 64
                    }) {
                for (int h : {
                            64, 256
                        }) {
                    for (int w : {
                                64, 256
                            }) {
                        add_inputs_shape(Shape({n, c, h, w}));
                    }
                }
            }
        }
    }
    void fill_inputs(float minv, float maxv) {
        int input_size = _inputs_dev.size();
        CHECK_EQ(input_size, _inputs_host.size()) << "dev and host inputs num must be equal";

        if (_input_type == RANDOM) {
            for (int i = 0; i < _inputs_dev.size(); ++i) {
                for (int j = 0; j < _op_input_num; ++j) {
                    fill_tensor_rand(*_inputs_dev[i][j], minv, maxv);
                    // LOG(INFO) << "_op_input_num: " << _op_input_num;
                    _inputs_host[i][j] -> copy_from(*_inputs_dev[i][j]);
                }
            }
        } else {
            CHECK_EQ(input_size, 1) << "special input num must be 1";

            for (int i = 0; i < _inputs_dev.size(); ++i) {
                for (int j = 0; j < _op_input_num; ++j) {
                    fill_tensor_const(*_inputs_dev[i][j], _special_value);
                    _inputs_host[i][j] -> copy_from(*_inputs_dev[i][j]);
                }
            }
        }
    }
    void add_custom_input(Input_dt& input) {
        CHECK_EQ(input.size(), _op_input_num) << "input must equal op_input_num";
        //clear_datas();
        std::vector<Shape> shape_v;

        for (int i = 0; i < _op_input_num; ++i) {
            shape_v.push_back(input[i] -> valid_shape());
        }

        add_inputs_shape(shape_v);

        for (int i = 0; i < _op_input_num; ++i) {
            SaberStatus status = _inputs_dev[0][i]->set_dtype(input[i]->get_dtype());
            SaberStatus status2 = _inputs_host[0][i]->set_dtype(input[i]->get_dtype());

            if (status != SaberSuccess || status2 != SaberSuccess) {
                LOG(INFO) << "ERROR";
            }
            if (std::is_same<TargetType_D, MLU>::value) {
				TensorH temp_cpu;
				temp_cpu.re_alloc(input[i]->shape(), input[i]->get_dtype());
				temp_cpu.copy_from(*input[i]);
			    _inputs_dev[0][i] -> copy_from(temp_cpu);
//			    _inputs_dev[0][i] -> copy_from(*_inputs_host[0][i]);
                _inputs_host[0][i] -> copy_from(*input[i]);
            }else {
                _inputs_dev[0][i] -> copy_from(*input[i]);
                _inputs_host[0][i] -> copy_from(*input[i]);
			}
 
            if (input[i]->get_seq_offset().size() > 0) {
                _inputs_dev[0][i] -> set_seq_offset(input[i]->get_seq_offset());
                _inputs_host[0][i] -> set_seq_offset(input[i]->get_seq_offset());
            }
        }

        _input_type = CUSTOM;
    }

    void set_input_datatype(DataType dtype_in = AK_FLOAT) {
        _in_data_type = dtype_in;
    }
    void set_output_datatype(DataType dtype_out = AK_FLOAT) {
        _out_data_type = dtype_out;
    }

    void compute_outputs_shape(int param_index = 0) {
        CHECK_GT(_params.size(), 0) << "no available param";
        CHECK_GT(_inputs_dev.size(), 0) << "no available inputs";
        CHECK_GE(param_index, 0) << "param index must be positive";
        CHECK_EQ(_inputs_dev.size(), _outputs_dev.size()) << "inputs and outputs must have same num";
        CHECK_LT(param_index, _params.size()) << "param_index out of range";

        for (int i = 0; i < _inputs_dev.size(); ++i) {
            SABER_CHECK(_base_op.compute_output_shape(_inputs_dev[i],
                        _outputs_dev[i], _params[param_index]));
        }

        for (int i = 0; i < _outputs_dev.size(); ++i) {
            for (int j = 0; j < _op_output_num; ++j) {
                Shape sh = _outputs_dev[i][j] -> valid_shape();
                _outputs_dev[i][j] -> re_alloc(sh, _out_data_type);
                _outputs_host[i][j] -> re_alloc(sh, _out_data_type);
                _outputs_hd[i][j] -> re_alloc(sh, _out_data_type);

                if (!_use_random_output) {
                    fill_tensor_const(*_outputs_dev[i][j], 0);
                    fill_tensor_const(*_outputs_host[i][j], 0);
                } else {
                    fill_tensor_rand(*_outputs_dev[i][j], -5.f, 5.f);
                    _outputs_host[i][j]->copy_from(*_outputs_dev[i][j]);
                    _outputs_hd[i][j]->copy_from(*_outputs_dev[i][j]);
                }
            }
        }
    }

    template <typename TensorType>
    void clear_vv(std::vector<std::vector<TensorType*>>& data_vec) {
        for (auto vec : data_vec) {
            for (auto tensor_p : vec) {
                if (nullptr != tensor_p) {
                    delete tensor_p;
                }
            }
        }

        data_vec.clear();
    }
    void clear_datas() {
        clear_vv<TensorD>(_inputs_dev);
        clear_vv<TensorD>(_outputs_dev);
        clear_vv<TensorH>(_inputs_host);
        clear_vv<TensorH>(_outputs_host);
        clear_vv<TensorH>(_outputs_hd);
        _input_shapes.clear();
    }
    SaberStatus get_op_result(SaberImplStrategy strategy, ImplEnum implenum, int param_index = 0,
                              bool test_speed = false) {
        CHECK_GE(param_index, 0) << "param index must be positive";
        CHECK_LT(param_index, _params.size()) << "param index out of range";

        Context<TargetType_D> ctx(0, 1, 1);
        SaberStatus status = SaberSuccess;
        SaberTimer<TargetType_D> t;
        int iter_num = test_speed ? 100 : 1;
        t.clear();
        t.start(ctx);

        for (int input_index = 0; input_index < _inputs_dev.size(); ++input_index) {
            _base_op.init(_inputs_dev[input_index], _outputs_dev[input_index],
                          _params[param_index], strategy, implenum, ctx);
            auto out_num = _outputs_dev[input_index].size();

            for (int iter = 0; iter < iter_num; ++iter) {
                for (int out_id = 0; out_id < out_num; out_id++) {
                    _outputs_dev[input_index][out_id]->copy_from(*_outputs_host[input_index][out_id]);
                }

                status = _base_op(_inputs_dev[input_index], _outputs_dev[input_index],
                                  _params[param_index], ctx);

                if (status == SaberUnImplError) {
                    return status;
                }

                typename TensorD :: API :: stream_t stream = ctx.get_compute_stream();

                for (int out_id = 0; out_id < out_num; out_id++) {
                    _outputs_dev[input_index][out_id] -> record_event(stream);
                    _outputs_dev[input_index][out_id] -> sync();
                }

            }
        }

        t.end(ctx);
        float ts = t.get_average_ms();

        if (test_speed) {
            LOG(INFO) << "avg run time:" << ts / _inputs_dev.size() / 100 << "ms";
        }

        for (int input_index = 0; input_index < _inputs_dev.size(); ++input_index) {
            for (int j = 0; j < _op_output_num; ++j) {
                _outputs_hd[input_index][j]->reshape(_outputs_dev[input_index][j]->valid_shape());

                _outputs_hd[input_index][j] -> copy_from(*_outputs_dev[input_index][j]);
            }
        }

        return status;
    }
    void get_cpu_result(CpuFunc_t CpuFunc, int param_index = 0) {
        CHECK_EQ(_inputs_host.size(), _outputs_dev.size()) << "input and output number must be equal";
        CHECK_EQ(_outputs_host.size(), _outputs_dev.size()) << "input and output number must be equal";

        for (int i = 0; i < _inputs_dev.size(); ++i) {
            Input_ht fake_input;
            Output_ht fake_output;

            if (_inputs_host[i][0]->get_layout() == Layout_NCHW_C8R
                    || _inputs_host[i][0]->get_layout() == Layout_NCHW_C16R) {
                CHECK_EQ(_inputs_host[i].size(), 1) << "testbase only support one input now";
                CHECK_EQ(_outputs_host[i][0]->get_layout(),
                         _inputs_host[i][0]->get_layout()) << "test base only support same layout now";
                int num_in = _inputs_host[i][0]->num();
                int c_in = _inputs_host[i][0]->channel();
                int h_in = _inputs_host[i][0]->height();
                int w_in = _inputs_host[i][0]->width();
                int num_out = _outputs_host[i][0]->num();
                int c_out = _outputs_host[i][0]->channel();
                int h_out = _outputs_host[i][0]->height();
                int w_out = _outputs_host[i][0]->width();
                TensorH temp_in = TensorH(Shape({num_in, c_in, h_in, w_in}));
                TensorH temp_out = TensorH(Shape({num_out, c_out, h_out, w_out}));
                Input_ht input_vec;
                Output_ht output_vec;
                input_vec.push_back(&temp_in);
                output_vec.push_back(&temp_out);
                reorder_nchwc_nchw(*_inputs_host[i][0], temp_in);
                CpuFunc(input_vec, output_vec, _params[param_index]);
                reorder_nchwc_nchw(temp_out, *_outputs_host[i][0]);

            } else {
                CpuFunc(_inputs_host[i], _outputs_host[i], _params[param_index]);
            }
        }
    }
    void result_check_accuracy(double succ_ratio = 0.00001, bool write_error_tensor = false) {
        CHECK_EQ(_outputs_host.size(), _outputs_hd.size()) << "output size in dev and cpu must be equal";
        int check_size = _outputs_host.size();
        std::vector<double> max_diff(check_size, 0);
        std::vector<double> max_ratio(check_size, 0);
        Shape sh = _inputs_host[0][0] -> valid_shape();
        LayoutType lo = _inputs_host[0][0] -> get_layout();

        for (int i = 0; i < _outputs_host.size(); ++i) {
            for (int j = 0; j < _op_output_num; ++j) {
                tensor_cmp_host<OpDataType>(static_cast<const OpDataType*>(_outputs_hd[i][j] -> data()),
                                            static_cast<const OpDataType*>(_outputs_host[i][j] -> data()),
                                            _outputs_hd[i][j] -> valid_size(), max_ratio[i], max_diff[i]);
                LOG(INFO) << "input_shape: (" << sh.num() << "," << sh.channel() << "," << sh.height() << "," <<
                          sh.width() << ")";
                LOG(INFO) << "input_layout = " << lo;
                LOG(INFO) << "max_ratio: " << max_ratio[i] << ", max diff: " << max_diff[i];
                LOG(INFO) << " mean_value: " << tensor_mean_value(*_outputs_hd[i][j]) << "," << tensor_mean_value(
                              *_outputs_host[i][j]);
                LOG(INFO) << " output shape: " << _outputs_hd[i][j]->valid_shape();
                LOG(INFO) << " output layout: " << _outputs_hd[i][j]->get_layout();

                if ((max_diff[i] < 0.001 || max_ratio[i] <= succ_ratio)
                        && (_outputs_hd[i][0]->valid_shape() == _outputs_host[i][0]->valid_shape()) \
                        && _outputs_hd[i][0]->get_layout() == _outputs_host[i][0]->get_layout()) {
                    LOG(INFO) << "Test Passed!";

                } else {
                    LOG(INFO) << "max_ratio: " << max_ratio[i] << ", max diff: " << max_diff[i];

                    if (write_error_tensor) {
                        char target_file_name[100];
                        char host_file_name[100];
                        sprintf(target_file_name, "error_target_output_%d", j);
                        sprintf(host_file_name, "error_host_output_%d", j);
                        write_tensorfile(*_outputs_hd[i][j], target_file_name);
                        write_tensorfile(*_outputs_host[i][j], host_file_name);
                    }

                    print_tensor(*_inputs_host[0][0]);
                    //print_tensor(*_inputs_host[0][1]);
                    print_tensor(*_outputs_host[0][0]);
                    print_tensor(*_outputs_hd[0][0]);
                    LOG(FATAL) << "Test Failed!!" << "output:(" << i << "-" << j << ")";

                }
            }
        }
    }
    void set_rand_limit(float minv, float maxv) {
        _max_value = maxv;
        _min_value = minv;
    }
    void run_test(CpuFunc_t CpuFunc, double succ_ratio = 0.00001, bool write_error_tensor = false,
                  bool test_speed = false) {
        if (_input_type == SPECIAL) {
            fill_inputs(_special_value, _special_value);
        }

        if (_input_type == RANDOM) {
            fill_inputs(_min_value, _max_value);
        }

        // LOG(INFO) << "_input_type" << _input_type;
        compute_outputs_shape();

        std :: vector<std :: string> runtype{"STATIC", "RUNTIME", "SPECIFY"};
        std :: vector<std :: string> impltype{"VENDER", "SABER"};

        for (auto strate : {
                    SPECIFY, RUNTIME, STATIC
                }) {
            for (auto implenum : {
                        VENDER_IMPL, SABER_IMPL
                    }) {
                LOG(INFO) << "TESTING: strategy:" << runtype[strate - 1] << ",impltype:" << impltype[(int)implenum];
#ifdef USE_ARM_PLACE

                for (auto th : {
                            1, 2, 4
                        }) {
                    Context<ARM> ctx;
                    LOG(INFO) << "create runtime ctx";
                    ctx.set_run_mode(SABER_POWER_HIGH, th);
                    LOG(INFO) << "test threads activated";
                    LOG(INFO) << "number of threads: " << th;
#ifdef USE_OPENMP
                    #pragma omp parallel
                    {
                        int thread = omp_get_num_threads();
                        LOG(INFO) << "number of omp threads: " << thread;
                    }
#endif

                    if (get_op_result(strate, implenum, 0, test_speed) == SaberUnImplError) {
                        LOG(INFO) << "Unimpl!!";
                        continue;
                    }

                    get_cpu_result(CpuFunc);
                    result_check_accuracy(succ_ratio, write_error_tensor);
                }
#else

                if (get_op_result(strate, implenum, 0, test_speed) == SaberUnImplError) {
                    LOG(INFO) << "Unimpl!!";
                    continue;
                }

                get_cpu_result(CpuFunc);
                result_check_accuracy(succ_ratio, write_error_tensor);
#endif
            }
        }
    }
    void result_check_speed() {
    }
    void set_random_output(bool random_output) {
        _use_random_output = random_output;
    }
private:
    int _op_input_num;
    int _op_output_num;
    Op_t _base_op;
    TestDataType _input_type;
    OpDataType _special_value;
    DataType _out_data_type = AK_FLOAT;
    DataType _in_data_type = AK_FLOAT;
    float _max_value{100.0};
    float _min_value{-100.0};

    std :: vector<Input_ht> _inputs_host;
    std :: vector<Input_dt> _inputs_dev;
    std :: vector<Output_dt> _outputs_dev;
    std :: vector<Output_ht> _outputs_host;
    std :: vector<Output_ht> _outputs_hd;
    std :: vector<std::vector<Shape>> _input_shapes;
    std :: vector<Param_t> _params;
    bool _use_random_output{false};
};//testsaberbase
}//namespace saber
}//namespace anakin

#endif //ANAKIN_TEST_SABER_BASE_H
