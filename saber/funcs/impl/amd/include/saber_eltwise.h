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
#ifndef ANAKIN_SABER_FUNCS_IMPL_AMD_SABER_ELTWISE_H
#define ANAKIN_SABER_FUNCS_IMPL_AMD_SABER_ELTWISE_H

#include "saber/funcs/base.h"
#include "saber/core/impl/amd/utils/amd_base.h"
#include "saber/funcs/impl/impl_eltwise.h"
#include "saber/funcs/impl/amd/include/saber_activation.h"
#include "saber/saber_types.h"
#include "saber/funcs/impl/impl_base.h"
#include "saber/saber_funcs_param.h"
#include "saber/core/impl/amd/utils/amd_kernel.h"

namespace anakin {
namespace saber {

#define ELT_PRODUCTION 0
#define ELT_SUM 0
#define ELT_MAX 0

template <DataType OpDtype>
class SaberEltwise<AMD, OpDtype> : public ImplBase<AMD, OpDtype, EltwiseParam<AMD>> {
public:
    typedef typename DataTrait<AMD, OpDtype>::Dtype OpDataType;
    typedef AMD_API::TPtr PtrDtype;

    SaberEltwise()
        : _with_relu(false)
        , _other_activation(false)
    {}

    ~SaberEltwise() {}

    virtual SaberStatus
    init(const std::vector<Tensor<AMD>*>& inputs,
         std::vector<Tensor<AMD>*>& outputs,
         EltwiseParam<AMD>& param,
         Context<AMD>& ctx) override;

    virtual SaberStatus
    create(const std::vector<Tensor<AMD>*>& inputs,
           std::vector<Tensor<AMD>*>& outputs,
           EltwiseParam<AMD>& param,
           Context<AMD>& ctx) override;

    virtual SaberStatus dispatch(
        const std::vector<Tensor<AMD>*>& inputs,
        std::vector<Tensor<AMD>*>& outputs,
        EltwiseParam<AMD>& param) override;

private:
    bool _with_relu;
    bool _other_activation;
    SaberActivation<AMD, OpDtype> _saber_activation;
    std::vector<AMDKernelPtr> _kernels_ptr {nullptr};
};

} // namespace saber
} // namespace anakin
#endif
