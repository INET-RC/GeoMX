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

#include <stdint.h>
#include "mkldnn_test_common.hpp"
#include "gtest/gtest.h"

#include "mkldnn.hpp"
#include "test_convolution_relu_forward_common.hpp"
namespace mkldnn {

using convolution_test = convolution_relu_test<int16_t, int16_t,
                                               int32_t, int32_t>;

TEST_P(convolution_test, TestConvolution)
{
}

#define S16S16S32
#define DIRECTION_FORWARD
#include "convolution_common.h"

}