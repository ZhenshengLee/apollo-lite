/******************************************************************************
 * Copyright 2018 The Apollo Authors. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *****************************************************************************/
#include <NvInferPlugin.h>
#include <NvInferRuntimeCommon.h>
#include <NvInferVersion.h>

#ifdef NV_TENSORRT_MAJOR
#if NV_TENSORRT_MAJOR == 8
#include "modules/perception/inference/tensorrt/rt_legacy.h"
#endif
#endif
#include <sys/stat.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <cuda_runtime_api.h>

#include "absl/strings/str_cat.h"

#include "cyber/common/log.h"
#include "modules/perception/inference/tensorrt/plugins/argmax_plugin.h"
#include "modules/perception/inference/tensorrt/plugins/dfmb_psroi_align_plugin.h"
#include "modules/perception/inference/tensorrt/plugins/leakyReLU_plugin.h"
#include "modules/perception/inference/tensorrt/plugins/rcnn_proposal_plugin.h"
#include "modules/perception/inference/tensorrt/plugins/rpn_proposal_ssd_plugin.h"
#include "modules/perception/inference/tensorrt/plugins/slice_plugin.h"
#include "modules/perception/inference/tensorrt/plugins/softmax_plugin.h"
#include "modules/perception/inference/tensorrt/rt_common.h"
#include "modules/perception/inference/tensorrt/rt_net.h"

class RTLogger : public nvinfer1::ILogger {
  void log(Severity severity, const char *msg) noexcept override {
    switch (severity) {
      case Severity::kINTERNAL_ERROR:
      case Severity::kERROR:
        AERROR << msg;
        break;
      case Severity::kWARNING:
        AWARN << msg;
        break;
      case Severity::kINFO:
      case Severity::kVERBOSE:
        ADEBUG << msg;
        break;
      default:
        break;
    }
  }
} rt_gLogger;
#define LOAD_DEBUG 0

namespace apollo {
namespace perception {
namespace inference {

void RTNet::ConstructMap(const LayerParameter &layer_param,
                         nvinfer1::ILayer *layer, TensorMap *tensor_map,
                         TensorModifyMap *tensor_modify_map) {
  for (int i = 0; i < layer_param.top_size(); i++) {
    std::string top_name = layer_param.top(i);
    TensorModifyMap::iterator it;
    it = tensor_modify_map->find(top_name);
    if (it != tensor_modify_map->end()) {
      std::string modify_name = top_name + "_" + layer_param.name();
      top_name = modify_name;
      it->second = top_name;
    } else {
      tensor_modify_map->insert(
          std::pair<std::string, std::string>(top_name, top_name));
    }
    tensor_map->insert(std::pair<std::string, nvinfer1::ITensor *>(
        top_name, layer->getOutput(i)));
    layer->getOutput(i)->setName(top_name.c_str());
  }
}

void RTNet::addConvLayer(const LayerParameter &layer_param,
                         nvinfer1::ITensor *const *inputs,
                         WeightMap *weight_map,
                         nvinfer1::INetworkDefinition *net,
                         TensorMap *tensor_map,
                         TensorModifyMap *tensor_modify_map) {
  ConvolutionParameter p = layer_param.convolution_param();

  int nbOutputs = p.num_output();

  int kernelH = p.has_kernel_h() ? p.kernel_h() : p.kernel_size(0);
  int kernelW = p.has_kernel_w()           ? p.kernel_w()
                : p.kernel_size_size() > 1 ? p.kernel_size(1)
                                           : p.kernel_size(0);
  int C = getCHW(inputs[0]->getDimensions()).c();
  int G = p.has_group() ? p.group() : 1;

  int size = nbOutputs * kernelW * kernelH * C;
  auto wt = loadLayerWeights(p.weight_filler().value(), size);
  nvinfer1::Weights bias_weight{nvinfer1::DataType::kFLOAT, nullptr, 0};

  auto inTensor = inputs[0];
  auto convLayer =
      net->addConvolution(*inTensor, nbOutputs,
                          nvinfer1::DimsHW{kernelH, kernelW}, wt, bias_weight);

  if (convLayer) {
    int strideH = p.has_stride_h()      ? p.stride_h()
                  : p.stride_size() > 0 ? p.stride(0)
                                        : 1;
    int strideW = p.has_stride_w()      ? p.stride_w()
                  : p.stride_size() > 1 ? p.stride(1)
                  : p.stride_size() > 0 ? p.stride(0)
                                        : 1;

    int padH = p.has_pad_h() ? p.pad_h() : p.pad_size() > 0 ? p.pad(0) : 0;
    int padW = p.has_pad_w()      ? p.pad_w()
               : p.pad_size() > 1 ? p.pad(1)
               : p.pad_size() > 0 ? p.pad(0)
                                  : 0;

    int dilationH = p.dilation_size() > 0 ? p.dilation(0) : 1;
    int dilationW = p.dilation_size() > 1   ? p.dilation(1)
                    : p.dilation_size() > 0 ? p.dilation(0)
                                            : 1;

    convLayer->setStride(nvinfer1::DimsHW{strideH, strideW});
    convLayer->setPadding(nvinfer1::DimsHW{padH, padW});
    convLayer->setPaddingMode(nvinfer1::PaddingMode::kCAFFE_ROUND_DOWN);
    convLayer->setDilation(nvinfer1::DimsHW{dilationH, dilationW});

    convLayer->setNbGroups(G);
  }

  if ((*weight_map)[layer_param.name().c_str()].size() > 0) {
    convLayer->setKernelWeights((*weight_map)[layer_param.name().c_str()][0]);

    if ((*weight_map)[layer_param.name().c_str()].size() > 1) {
      convLayer->setBiasWeights((*weight_map)[layer_param.name().c_str()][1]);
    }
  }
  std::vector<nvinfer1::Weights> lw;
  lw.resize(2);
  lw[0] = convLayer->getKernelWeights();
  lw[1] = convLayer->getBiasWeights();
  (*weight_map)[layer_param.name().c_str()] = lw;

  convLayer->setName(layer_param.name().c_str());
  ConstructMap(layer_param, convLayer, tensor_map, tensor_modify_map);

#if LOAD_DEBUG
  auto tmp_out_dims = convLayer->getOutput(0)->getDimensions();
  std::string dim_string = "input: ";
  for (int i = 0; i < 3; i++) {
    absl::StrAppend(&dim_string, " ",
                    convLayer->getInput(0)->getDimensions().d[i]);
  }
  absl::StrAppend(&dim_string, " | output: ");
  for (int i = 0; i < 3; i++) {
    absl::StrAppend(&dim_string, " ", tmp_out_dims.d[i]);
  }
  AINFO << layer_param.name() << dim_string;
#endif
}

void RTNet::addDeconvLayer(const LayerParameter &layer_param,
                           nvinfer1::ITensor *const *inputs,
                           WeightMap *weight_map,
                           nvinfer1::INetworkDefinition *net,
                           TensorMap *tensor_map,
                           TensorModifyMap *tensor_modify_map) {
  ConvolutionParameter conv = layer_param.convolution_param();
  ConvParam param;
  ParserConvParam(conv, &param);
  nvinfer1::IDeconvolutionLayer *deconvLayer = nullptr;
  if ((*weight_map)[layer_param.name().c_str()].size() == 2) {
    deconvLayer =
        net->addDeconvolution(*inputs[0], conv.num_output(),
                              nvinfer1::DimsHW{param.kernel_h, param.kernel_w},
                              (*weight_map)[layer_param.name().c_str()][0],
                              (*weight_map)[layer_param.name().c_str()][1]);
  } else {
    nvinfer1::Weights bias_weight{nvinfer1::DataType::kFLOAT, nullptr, 0};
    deconvLayer = net->addDeconvolution(
        *inputs[0], conv.num_output(),
        nvinfer1::DimsHW{param.kernel_h, param.kernel_w},
        (*weight_map)[layer_param.name().c_str()][0], bias_weight);
  }
  deconvLayer->setStride(nvinfer1::DimsHW{param.stride_h, param.stride_w});
  deconvLayer->setPadding(nvinfer1::DimsHW{param.padding_h, param.padding_w});
  deconvLayer->setPaddingMode(nvinfer1::PaddingMode::kCAFFE_ROUND_DOWN);
  deconvLayer->setNbGroups(conv.group());

  deconvLayer->setName(layer_param.name().c_str());
  ConstructMap(layer_param, deconvLayer, tensor_map, tensor_modify_map);

#if LOAD_DEBUG
  std::string dim_string = "input: ";
  for (int i = 0; i < 3; i++) {
    absl::StrAppend(&dim_string, " ", inputs[0]->getDimensions().d[i]);
  }
  absl::StrAppend(&dim_string, " | output: ");
  AINFO << layer_param.name() << dim_string;
#endif
}
void RTNet::addActiveLayer(const LayerParameter &layer_param,
                           nvinfer1::ITensor *const *inputs, int nbInputs,
                           nvinfer1::INetworkDefinition *net,
                           TensorMap *tensor_map,
                           TensorModifyMap *tensor_modify_map) {
  if (layer_param.type() == "ReLU" &&
      layer_param.relu_param().negative_slope() > 0.0f) {
    std::shared_ptr<ReLUPlugin> relu_plugin;
    // TODO(All): refactor it
    // convert proto message to pure struct to avoid adding protobuf as
    // cuda_library's dependence, which may case compile issue of nvcc
    StReLUParameter relu_param;
    relu_param.negative_slope = layer_param.relu_param().negative_slope();
    relu_param.engine = static_cast<int32_t>(layer_param.relu_param().engine());
    relu_plugin.reset(new ReLUPlugin(relu_param, inputs[0]->getDimensions()));
#ifdef NV_TENSORRT_MAJOR
#if NV_TENSORRT_MAJOR != 8
    nvinfer1::IPluginLayer *ReLU_Layer =
        net->addPlugin(inputs, nbInputs, *relu_plugin);
#else
    nvinfer1::IPluginV2Layer *ReLU_Layer =
        net->addPluginV2(inputs, nbInputs, *relu_plugin);
#endif
#endif
    relu_plugins_.push_back(relu_plugin);
    ReLU_Layer->setName(layer_param.name().c_str());
    ConstructMap(layer_param, ReLU_Layer, tensor_map, tensor_modify_map);
  } else {
    nvinfer1::ActivationType type = nvinfer1::ActivationType::kSIGMOID;
    auto pair = active_map.find(layer_param.type());
    if (pair != active_map.end()) {
      type = pair->second;
    }
    nvinfer1::IActivationLayer *reluLayer =
        net->addActivation(*inputs[0], type);
    reluLayer->setName(layer_param.name().c_str());

    ConstructMap(layer_param, reluLayer, tensor_map, tensor_modify_map);
  }
}

void RTNet::addConcatLayer(const LayerParameter &layer_param,
                           nvinfer1::ITensor *const *inputs, int nbInputs,
                           nvinfer1::INetworkDefinition *net,
                           TensorMap *tensor_map,
                           TensorModifyMap *tensor_modify_map) {
  ConcatParameter concat = layer_param.concat_param();
  nvinfer1::IConcatenationLayer *concatLayer =
      net->addConcatenation(inputs, nbInputs);
  // tensorrt ignore the first channel(batch channel), so when load caffe
  // model axis should -1
  concatLayer->setAxis(concat.axis() - 1);
  concatLayer->setName(layer_param.name().c_str());
  CHECK_EQ(nbInputs, layer_param.bottom_size());

#if LOAD_DEBUG
  std::string dim_string = "";
  for (int i = 0; i < nbInputs; i++) {
    auto dim_tmp = inputs[i]->getDimensions();
    for (int i = 0; i < 3; i++) {
      absl::StrAppend(&dim_string, " ", dim_tmp.d[i]);
    }
    AINFO << layer_param.name() << ": " << layer_param.bottom(i) << " "
          << (*tensor_modify_map)[layer_param.bottom(i)] << " " << dim_string;
  }
#endif
  ConstructMap(layer_param, concatLayer, tensor_map, tensor_modify_map);
}

void RTNet::addPoolingLayer(const LayerParameter &layer_param,
                            nvinfer1::ITensor *const *inputs,
                            nvinfer1::INetworkDefinition *net,
                            TensorMap *tensor_map,
                            TensorModifyMap *tensor_modify_map) {
  PoolingParameter pool = layer_param.pooling_param();
  nvinfer1::PoolingType pool_type =
      (pool.pool() == PoolingParameter_PoolMethod_MAX)
          ? nvinfer1::PoolingType::kMAX
          : nvinfer1::PoolingType::kAVERAGE;
  nvinfer1::PaddingMode padding_mode = nvinfer1::PaddingMode::kCAFFE_ROUND_UP;
  if (pool.has_round_mode()) {
    switch (static_cast<int>(pool.round_mode())) {
      case 0:
        padding_mode = nvinfer1::PaddingMode::kCAFFE_ROUND_UP;
        break;
      case 1:
        padding_mode = nvinfer1::PaddingMode::kCAFFE_ROUND_DOWN;
        break;
      default:
        padding_mode = nvinfer1::PaddingMode::kCAFFE_ROUND_UP;
    }
  }
  ACHECK(modify_pool_param(&pool));
  nvinfer1::IPoolingLayer *poolLayer = net->addPooling(
      *inputs[0], pool_type,
      {static_cast<int>(pool.kernel_h()), static_cast<int>(pool.kernel_w())});
  poolLayer->setStride(nvinfer1::DimsHW{static_cast<int>(pool.stride_h()),
                                        static_cast<int>(pool.stride_w())});
  poolLayer->setPadding(nvinfer1::DimsHW{static_cast<int>(pool.pad_h()),
                                         static_cast<int>(pool.pad_w())});
  poolLayer->setPaddingMode(padding_mode);
  // unlike other frameworks, caffe use inclusive counting for padded averaging
  poolLayer->setAverageCountExcludesPadding(false);
  poolLayer->setName(layer_param.name().c_str());
  ConstructMap(layer_param, poolLayer, tensor_map, tensor_modify_map);
}

void RTNet::addSliceLayer(const LayerParameter &layer_param,
                          nvinfer1::ITensor *const *inputs, int nbInputs,
                          nvinfer1::INetworkDefinition *net,
                          TensorMap *tensor_map,
                          TensorModifyMap *tensor_modify_map) {
#ifdef NV_TENSORRT_MAJOR
#if NV_TENSORRT_MAJOR != 8
  CHECK_GT(layer_param.slice_param().axis(), 0);
  std::shared_ptr<SLICEPlugin> slice_plugin;

  // TODO(All): refactor it
  // convert proto message to pure struct to avoid adding protobuf as
  // cuda_library's dependence, which may case compile issue of nvcc
  StSliceParameter slice_param;
  slice_param.axis = layer_param.slice_param().axis();
  slice_param.slice_sim = layer_param.slice_param().slice_dim();
  slice_param.slice_point.reserve(layer_param.slice_param().slice_point_size());
  for (int i = 0; i < layer_param.slice_param().slice_point_size(); ++i) {
    slice_param.slice_point[i] = layer_param.slice_param().slice_point(i);
  }
  slice_plugin.reset(new SLICEPlugin(slice_param, inputs[0]->getDimensions()));
  nvinfer1::IPluginLayer *sliceLayer =
      net->addPlugin(inputs, nbInputs, *slice_plugin);
  slice_plugins_.push_back(slice_plugin);
  sliceLayer->setName(layer_param.name().c_str());
  ConstructMap(layer_param, sliceLayer, tensor_map, tensor_modify_map);
#else
  const auto mPluginRegistry = getPluginRegistry();
  auto creator =
      mPluginRegistry->getPluginCreator("slice_plugin", "1", "camera");

  auto slice_param = layer_param.slice_param();
  int axis = slice_param.axis();
  int slice_point_num = slice_param.slice_point_size();
  std::vector<int> slice_point(slice_point_num);

  for (int i = 0; i < slice_point_num; i++) {
    slice_point[i] = slice_param.slice_point(i);
  }
  nvinfer1::Dims in_dims = inputs[0]->getDimensions();
  std::vector<nvinfer1::PluginField> f;
  f.emplace_back(nvinfer1::PluginField("axis", &axis,
                                       nvinfer1::PluginFieldType::kINT32, 1));
  f.emplace_back(nvinfer1::PluginField("slice_point", &slice_point[0],
                                       nvinfer1::PluginFieldType::kINT32,
                                       slice_point_num));
  f.emplace_back(nvinfer1::PluginField("in_dims", &in_dims,
                                       nvinfer1::PluginFieldType::kDIMS, 1));
  nvinfer1::PluginFieldCollection fc;
  fc.nbFields = f.size();
  fc.fields = f.data();

  nvinfer1::IPluginV2 *slice_plugin = creator->createPlugin("slice_layer", &fc);

  nvinfer1::IPluginV2Layer *sliceLayer =
      net->addPluginV2(inputs, nbInputs, *slice_plugin);
  sliceLayer->setName(layer_param.name().c_str());

  ConstructMap(layer_param, sliceLayer, tensor_map, tensor_modify_map);
#endif
#endif
}

void RTNet::addInnerproductLayer(const LayerParameter &layer_param,
                                 nvinfer1::ITensor *const *inputs,
                                 WeightMap *weight_map,
                                 nvinfer1::INetworkDefinition *net,
                                 TensorMap *tensor_map,
                                 TensorModifyMap *tensor_modify_map) {
  InnerProductParameter fc = layer_param.inner_product_param();
  nvinfer1::Weights bias{nvinfer1::DataType::kFLOAT, nullptr, 0};
  if ((*weight_map)[layer_param.name().c_str()].size() == 2) {
    bias = (*weight_map)[layer_param.name().c_str()][1];
  }
  nvinfer1::IFullyConnectedLayer *fcLayer = net->addFullyConnected(
      *inputs[0], fc.num_output(), (*weight_map)[layer_param.name().c_str()][0],
      bias);
  fcLayer->setName(layer_param.name().c_str());
  ConstructMap(layer_param, fcLayer, tensor_map, tensor_modify_map);
}

void RTNet::addScaleLayer(const LayerParameter &layer_param,
                          nvinfer1::ITensor *const *inputs,
                          WeightMap *weight_map,
                          nvinfer1::INetworkDefinition *net,
                          TensorMap *tensor_map,
                          TensorModifyMap *tensor_modify_map) {
  std::vector<nvinfer1::Weights> lw(
      3, nvinfer1::Weights{nvinfer1::DataType::kFLOAT, nullptr, 0});
  if (layer_param.type() == "Power") {
    auto power_param = layer_param.power_param();
    nvinfer1::Dims input_dim = inputs[0]->getDimensions();
    int size = input_dim.d[0];
    lw[0] = loadLayerWeights(power_param.scale(), size);
    lw[1] = loadLayerWeights(power_param.shift(), size);
    lw[2] = loadLayerWeights(power_param.power(), size);
  } else {
    ACHECK(weight_map->find(layer_param.name().c_str()) != weight_map->end());
    for (size_t i = 0; i < (*weight_map)[layer_param.name().c_str()].size();
         i++) {
      lw[i] = (*weight_map)[layer_param.name().c_str()][i];
    }
  }
  (*weight_map)[layer_param.name().c_str()] = lw;

  nvinfer1::IScaleLayer *scaleLayer = net->addScale(
      *inputs[0], nvinfer1::ScaleMode::kCHANNEL, lw[1], lw[0], lw[2]);
  scaleLayer->setName(layer_param.name().c_str());
  ConstructMap(layer_param, scaleLayer, tensor_map, tensor_modify_map);
}

void RTNet::addBatchnormLayer(const LayerParameter &layer_param,
                              nvinfer1::ITensor *const *inputs,
                              WeightMap *weight_map,
                              nvinfer1::INetworkDefinition *net,
                              TensorMap *tensor_map,
                              TensorModifyMap *tensor_modify_map) {
  BatchNormParameter param = layer_param.batch_norm_param();
  nvinfer1::Weights power{nvinfer1::DataType::kFLOAT, nullptr, 0};
  // shift scale power
  nvinfer1::IScaleLayer *scaleLayer =
      net->addScale(*inputs[0], nvinfer1::ScaleMode::kCHANNEL,
                    (*weight_map)[layer_param.name().c_str()][0],
                    (*weight_map)[layer_param.name().c_str()][1], power);
  scaleLayer->setName(layer_param.name().c_str());

  ConstructMap(layer_param, scaleLayer, tensor_map, tensor_modify_map);
}
void RTNet::addSoftmaxLayer(const LayerParameter &layer_param,
                            nvinfer1::ITensor *const *inputs, int nbInputs,
                            nvinfer1::INetworkDefinition *net,
                            TensorMap *tensor_map,
                            TensorModifyMap *tensor_modify_map) {
  if (layer_param.has_softmax_param()) {
#ifdef NV_TENSORRT_MAJOR
#if NV_TENSORRT_MAJOR != 8
    std::shared_ptr<SoftmaxPlugin> softmax_plugin;
    // TODO(All): refactor it
    // convert proto message to pure struct to avoid adding protobuf as
    // cuda_library's dependence, which may case compile issue of nvcc
    StSoftmaxParameter softmax_param;
    softmax_param.engine =
        static_cast<int32_t>(layer_param.softmax_param().engine());
    sofmax_param.axis = layer_param.softmax_param().axis();
    softmax_plugin.reset(
        new SoftmaxPlugin(softmax_param, inputs[0]->getDimensions()));
    softmax_plugins_.push_back(softmax_plugin);
    nvinfer1::IPluginLayer *softmaxLayer =
        net->addPlugin(inputs, nbInputs, *softmax_plugin);
    softmaxLayer->setName(layer_param.name().c_str());

    ConstructMap(layer_param, softmaxLayer, tensor_map, tensor_modify_map);
#else
    const auto mPluginRegistry = getPluginRegistry();
    auto creator =
        mPluginRegistry->getPluginCreator("softmax_plugin", "1", "camera");

    auto softmax_param = layer_param.softmax_param();
    int axis = softmax_param.axis();
    nvinfer1::Dims in_dims = inputs[0]->getDimensions();
    std::vector<nvinfer1::PluginField> f;
    f.emplace_back(nvinfer1::PluginField("axis", &axis,
                                         nvinfer1::PluginFieldType::kINT32, 1));
    f.emplace_back(nvinfer1::PluginField("in_dims", &in_dims,
                                         nvinfer1::PluginFieldType::kDIMS, 1));

    nvinfer1::PluginFieldCollection fc;
    fc.nbFields = f.size();
    fc.fields = f.data();
    nvinfer1::IPluginV2 *softmax_plugin =
        creator->createPlugin("softmax_layer", &fc);

    nvinfer1::IPluginV2Layer *softmaxLayer =
        net->addPluginV2(inputs, nbInputs, *softmax_plugin);
    softmaxLayer->setName(layer_param.name().c_str());
    ConstructMap(layer_param, softmaxLayer, tensor_map, tensor_modify_map);
#endif
#endif
  } else {
    nvinfer1::ISoftMaxLayer *softmaxLayer = net->addSoftMax(*inputs[0]);
    softmaxLayer->setName(layer_param.name().c_str());
    ConstructMap(layer_param, softmaxLayer, tensor_map, tensor_modify_map);
  }
}

void RTNet::addEltwiseLayer(const LayerParameter &layer_param,
                            nvinfer1::ITensor *const *inputs,
                            nvinfer1::INetworkDefinition *net,
                            TensorMap *tensor_map,
                            TensorModifyMap *tensor_modify_map) {
  auto pair = eltwise_map.find(layer_param.eltwise_param().operation());
  nvinfer1::ElementWiseOperation op = nvinfer1::ElementWiseOperation::kSUM;
  if (pair != eltwise_map.end()) {
    op = pair->second;
  }
  nvinfer1::IElementWiseLayer *eltwiseLayer =
      net->addElementWise(*inputs[0], *inputs[1], op);

  eltwiseLayer->setName(layer_param.name().c_str());
  ConstructMap(layer_param, eltwiseLayer, tensor_map, tensor_modify_map);
}

void RTNet::addArgmaxLayer(const LayerParameter &layer_param,
                           nvinfer1::ITensor *const *inputs, int nbInputs,
                           nvinfer1::INetworkDefinition *net,
                           TensorMap *tensor_map,
                           TensorModifyMap *tensor_modify_map) {
  std::shared_ptr<ArgMax1Plugin> argmax_plugin;

  // TODO(All): refactor it
  // convert proto message to pure struct to avoid adding protobuf as
  // cuda_library's dependence, which may case compile issue of nvcc
  StArgMaxParameter argmax_param;
  argmax_param.out_max_val = layer_param.argmax_param().out_max_val();
  argmax_param.top_k = layer_param.argmax_param().top_k();
  argmax_param.axis = layer_param.argmax_param().axis();
  argmax_plugin.reset(
      new ArgMax1Plugin(argmax_param, inputs[0]->getDimensions()));
  argmax_plugins_.push_back(argmax_plugin);
#ifdef NV_TENSORRT_MAJOR
#if NV_TENSORRT_MAJOR != 8
  nvinfer1::IPluginLayer *argmaxLayer =
      net->addPlugin(inputs, nbInputs, *argmax_plugin);
#else
  nvinfer1::IPluginV2Layer *argmaxLayer =
      net->addPluginV2(inputs, nbInputs, *argmax_plugin);
#endif
#endif
  argmaxLayer->setName(layer_param.name().c_str());
  ConstructMap(layer_param, argmaxLayer, tensor_map, tensor_modify_map);
}

void RTNet::addPermuteLayer(const LayerParameter &layer_param,
                            nvinfer1::ITensor *const *inputs, int nbInputs,
                            nvinfer1::INetworkDefinition *net,
                            TensorMap *tensor_map,
                            TensorModifyMap *tensor_modify_map) {
  CHECK_LE(layer_param.permute_param().order_size(), nvinfer1::Dims::MAX_DIMS);
  nvinfer1::IShuffleLayer *permuteLayer = net->addShuffle(*inputs[0]);
  nvinfer1::Permutation permutation;

  // For loading Caffe's permute param,
  // e.g. Caffe: [0, 2, 1, 3] -> TensorRT: [1, 0, 2], omitting 1st dim N.
  ACHECK(layer_param.permute_param().order(0) == 0);
  for (int i = 1; i < layer_param.permute_param().order_size(); ++i) {
    int order = layer_param.permute_param().order(i);
    permutation.order[i - 1] = order - 1;
  }
  permuteLayer->setFirstTranspose(permutation);

  permuteLayer->setName(layer_param.name().c_str());
  ConstructMap(layer_param, permuteLayer, tensor_map, tensor_modify_map);
}

void RTNet::addReshapeLayer(const LayerParameter &layer_param,
                            nvinfer1::ITensor *const *inputs,
                            nvinfer1::INetworkDefinition *net,
                            TensorMap *tensor_map,
                            TensorModifyMap *tensor_modify_map) {
  nvinfer1::DimsCHW dims;
  dims.d[0] = static_cast<int>(layer_param.reshape_param().shape().dim(1));
  dims.d[1] = static_cast<int>(layer_param.reshape_param().shape().dim(2));
  dims.d[2] = static_cast<int>(layer_param.reshape_param().shape().dim(3));
  nvinfer1::IShuffleLayer *reshapeLayer = net->addShuffle(*inputs[0]);
  reshapeLayer->setReshapeDimensions(dims);
  reshapeLayer->setName(layer_param.name().c_str());
  ConstructMap(layer_param, reshapeLayer, tensor_map, tensor_modify_map);
}

void RTNet::addPaddingLayer(const LayerParameter &layer_param,
                            nvinfer1::ITensor *const *inputs,
                            nvinfer1::INetworkDefinition *net,
                            TensorMap *tensor_map,
                            TensorModifyMap *tensor_modify_map) {
  nvinfer1::DimsHW pre_dims(layer_param.padding_param().pad_t(),
                            layer_param.padding_param().pad_l());
  nvinfer1::DimsHW post_dims(layer_param.padding_param().pad_b(),
                             layer_param.padding_param().pad_r());
  nvinfer1::IPaddingLayer *padding_layer =
      net->addPadding(*inputs[0], pre_dims, post_dims);
  padding_layer->setName(layer_param.name().c_str());
  ConstructMap(layer_param, padding_layer, tensor_map, tensor_modify_map);
}

void RTNet::addDFMBPSROIAlignLayer(const LayerParameter &layer_param,
                                   nvinfer1::ITensor *const *inputs,
                                   int nbInputs,
                                   nvinfer1::INetworkDefinition *net,
                                   TensorMap *tensor_map,
                                   TensorModifyMap *tensor_modify_map) {
  std::shared_ptr<DFMBPSROIAlignPlugin> dfmb_psroi_align_plugin;
  nvinfer1::Dims input_dims[3];
  for (int i = 0; i < nbInputs && i < 3; ++i) {
    input_dims[i] = inputs[i]->getDimensions();
  }
  // TODO(All): refactor it
  // convert proto message to pure struct to avoid adding protobuf as
  // cuda_library's dependence, which may case compile issue of nvcc
  StDFMBPSROIAlignParameter dfmb_psroi_pooling_param;
  dfmb_psroi_pooling_param.heat_map_a =
      layer_param.dfmb_psroi_pooling_param().heat_map_a();
  dfmb_psroi_pooling_param.output_dim =
      layer_param.dfmb_psroi_pooling_param().output_dim();
  dfmb_psroi_pooling_param.group_height =
      layer_param.dfmb_psroi_pooling_param().group_height();
  dfmb_psroi_pooling_param.group_width =
      layer_param.dfmb_psroi_pooling_param().group_width();
  dfmb_psroi_pooling_param.pooled_height =
      layer_param.dfmb_psroi_pooling_param().pooled_height();
  dfmb_psroi_pooling_param.pooled_width =
      layer_param.dfmb_psroi_pooling_param().pooled_width();
  dfmb_psroi_pooling_param.pad_ratio =
      layer_param.dfmb_psroi_pooling_param().pad_ratio();
  dfmb_psroi_pooling_param.sample_per_part =
      layer_param.dfmb_psroi_pooling_param().sample_per_part();
  dfmb_psroi_pooling_param.trans_std =
      layer_param.dfmb_psroi_pooling_param().trans_std();
  dfmb_psroi_pooling_param.part_height =
      layer_param.dfmb_psroi_pooling_param().part_height();
  dfmb_psroi_pooling_param.part_width =
      layer_param.dfmb_psroi_pooling_param().part_width();
  dfmb_psroi_pooling_param.heat_map_b =
      layer_param.dfmb_psroi_pooling_param().heat_map_b();
  dfmb_psroi_align_plugin.reset(
      new DFMBPSROIAlignPlugin(dfmb_psroi_pooling_param, input_dims, nbInputs));
  dfmb_psroi_align_plugins_.push_back(dfmb_psroi_align_plugin);
#ifdef NV_TENSORRT_MAJOR
#if NV_TENSORRT_MAJOR != 8
  nvinfer1::IPluginLayer *dfmb_psroi_align_layer =
      net->addPlugin(inputs, nbInputs, *dfmb_psroi_align_plugin);
#else
  nvinfer1::IPluginV2Layer *dfmb_psroi_align_layer =
      net->addPluginV2(inputs, nbInputs, *dfmb_psroi_align_plugin);
#endif
#endif
  dfmb_psroi_align_layer->setName(layer_param.name().c_str());

  ConstructMap(layer_param, dfmb_psroi_align_layer, tensor_map,
               tensor_modify_map);
}

void RTNet::addRCNNProposalLayer(const LayerParameter &layer_param,
                                 nvinfer1::ITensor *const *inputs, int nbInputs,
                                 nvinfer1::INetworkDefinition *net,
                                 TensorMap *tensor_map,
                                 TensorModifyMap *tensor_modify_map) {
  std::shared_ptr<RCNNProposalPlugin> rcnn_proposal_plugin;
  nvinfer1::Dims input_dims[4];
  for (int i = 0; i < nbInputs && i < 4; ++i) {
    input_dims[i] = inputs[i]->getDimensions();
  }
  // TODO(All): refactor it
  // convert proto message to pure struct to avoid adding protobuf as
  // cuda_library's dependence, which may case compile issue of nvcc
  StBBoxRegParameter bbox_reg_param;
  bbox_reg_param.bbox_mean.reserve(
      layer_param.bbox_reg_param().bbox_mean_size());
  for (int i = 0; i < layer_param.bbox_reg_param().bbox_mean_size(); ++i) {
    bbox_reg_param.bbox_mean[i] = layer_param.bbox_reg_param().bbox_mean(i);
  }
  bbox_reg_param.bbox_std.reserve(layer_param.bbox_reg_param().bbox_std_size());
  for (int i = 0; i < layer_param.bbox_reg_param().bbox_std_size(); ++i) {
    bbox_reg_param.bbox_std[i] = layer_param.bbox_reg_param().bbox_std(i);
  }
  StDetectionOutputSSDParameter detection_output_ssd_param;
  detection_output_ssd_param.heat_map_a =
      layer_param.detection_output_ssd_param().heat_map_a();
  detection_output_ssd_param.min_size_h =
      layer_param.detection_output_ssd_param().min_size_h();
  detection_output_ssd_param.min_size_w =
      layer_param.detection_output_ssd_param().min_size_w();
  detection_output_ssd_param.min_size_mode =
      layer_param.detection_output_ssd_param().min_size_mode();
  detection_output_ssd_param.threshold_objectness =
      layer_param.detection_output_ssd_param().threshold_objectness();
  detection_output_ssd_param.refine_out_of_map_bbox =
      layer_param.detection_output_ssd_param().refine_out_of_map_bbox();
  detection_output_ssd_param.num_class =
      layer_param.detection_output_ssd_param().num_class();
  detection_output_ssd_param.rpn_proposal_output_score =
      layer_param.detection_output_ssd_param().rpn_proposal_output_score();
  detection_output_ssd_param.regress_agnostic =
      layer_param.detection_output_ssd_param().regress_agnostic();

  detection_output_ssd_param.gen_anchor_param.anchor_width.reserve(
      layer_param.detection_output_ssd_param()
          .gen_anchor_param()
          .anchor_width_size());
  for (int i = 0; i < layer_param.detection_output_ssd_param()
                          .gen_anchor_param()
                          .anchor_width_size();
       ++i) {
    detection_output_ssd_param.gen_anchor_param.anchor_width[i] =
        layer_param.detection_output_ssd_param()
            .gen_anchor_param()
            .anchor_width(i);
  }
  detection_output_ssd_param.gen_anchor_param.anchor_height.reserve(
      layer_param.detection_output_ssd_param()
          .gen_anchor_param()
          .anchor_height_size());
  for (int i = 0; i < layer_param.detection_output_ssd_param()
                          .gen_anchor_param()
                          .anchor_height_size();
       ++i) {
    detection_output_ssd_param.gen_anchor_param.anchor_height[i] =
        layer_param.detection_output_ssd_param()
            .gen_anchor_param()
            .anchor_height(i);
  }
  detection_output_ssd_param.threshold.reserve(
      layer_param.detection_output_ssd_param().threshold_size());
  for (int i = 0; i < layer_param.detection_output_ssd_param().threshold_size();
       ++i) {
    detection_output_ssd_param.threshold[i] =
        layer_param.detection_output_ssd_param().threshold(i);
  }
  detection_output_ssd_param.nms_param.need_nms =
      layer_param.detection_output_ssd_param().nms_param().need_nms();
  detection_output_ssd_param.nms_param.overlap_ratio.reserve(
      layer_param.detection_output_ssd_param()
          .nms_param()
          .overlap_ratio_size());
  for (int i = 0; i < layer_param.detection_output_ssd_param()
                          .nms_param()
                          .overlap_ratio_size();
       ++i) {
    detection_output_ssd_param.nms_param.overlap_ratio[i] =
        layer_param.detection_output_ssd_param().nms_param().overlap_ratio(i);
  }
  detection_output_ssd_param.nms_param.top_n.reserve(
      layer_param.detection_output_ssd_param().nms_param().top_n_size());
  for (int i = 0;
       i < layer_param.detection_output_ssd_param().nms_param().top_n_size();
       ++i) {
    detection_output_ssd_param.nms_param.top_n[i] =
        layer_param.detection_output_ssd_param().nms_param().top_n(i);
  }
  detection_output_ssd_param.nms_param.max_candidate_n.reserve(
      layer_param.detection_output_ssd_param()
          .nms_param()
          .max_candidate_n_size());
  for (int i = 0; i < layer_param.detection_output_ssd_param()
                          .nms_param()
                          .max_candidate_n_size();
       ++i) {
    detection_output_ssd_param.nms_param.max_candidate_n[i] =
        layer_param.detection_output_ssd_param().nms_param().max_candidate_n(i);
  }
  detection_output_ssd_param.nms_param.use_soft_nms.reserve(
      layer_param.detection_output_ssd_param().nms_param().use_soft_nms_size());
  for (int i = 0;
       i <
       layer_param.detection_output_ssd_param().nms_param().use_soft_nms_size();
       ++i) {
    detection_output_ssd_param.nms_param.use_soft_nms[i] =
        layer_param.detection_output_ssd_param().nms_param().use_soft_nms(i);
  }
  detection_output_ssd_param.nms_param.voting.reserve(
      layer_param.detection_output_ssd_param().nms_param().voting_size());
  for (int i = 0;
       i < layer_param.detection_output_ssd_param().nms_param().voting_size();
       ++i) {
    detection_output_ssd_param.nms_param.voting[i] =
        layer_param.detection_output_ssd_param().nms_param().voting(i);
  }
  detection_output_ssd_param.nms_param.vote_iou.reserve(
      layer_param.detection_output_ssd_param().nms_param().vote_iou_size());
  for (int i = 0;
       i < layer_param.detection_output_ssd_param().nms_param().vote_iou_size();
       ++i) {
    detection_output_ssd_param.nms_param.vote_iou[i] =
        layer_param.detection_output_ssd_param().nms_param().vote_iou(i);
  }
  detection_output_ssd_param.nms_param.add_score =
      layer_param.detection_output_ssd_param().nms_param().add_score();
  detection_output_ssd_param.nms_param.nms_among_classes =
      layer_param.detection_output_ssd_param().nms_param().nms_among_classes();
  detection_output_ssd_param.nms_param.force_identity_iou_thr =
      layer_param.detection_output_ssd_param()
          .nms_param()
          .force_identity_iou_thr();
  detection_output_ssd_param.nms_param.force_imparity_iou_thr =
      layer_param.detection_output_ssd_param()
          .nms_param()
          .force_imparity_iou_thr();
  detection_output_ssd_param.nms_param.nms_gpu_max_n_per_time =
      layer_param.detection_output_ssd_param()
          .nms_param()
          .nms_gpu_max_n_per_time();

  rcnn_proposal_plugin.reset(new RCNNProposalPlugin(
      bbox_reg_param, detection_output_ssd_param, input_dims));
  rcnn_proposal_plugins_.push_back(rcnn_proposal_plugin);
#ifdef NV_TENSORRT_MAJOR
#if NV_TENSORRT_MAJOR != 8
  nvinfer1::IPluginLayer *rcnn_proposal_layer =
      net->addPlugin(inputs, nbInputs, *rcnn_proposal_plugin);
#else
  nvinfer1::IPluginV2Layer *rcnn_proposal_layer =
      net->addPluginV2(inputs, nbInputs, *rcnn_proposal_plugin);
#endif
#endif
  rcnn_proposal_layer->setName(layer_param.name().c_str());

  ConstructMap(layer_param, rcnn_proposal_layer, tensor_map, tensor_modify_map);
}

void RTNet::addRPNProposalSSDLayer(const LayerParameter &layer_param,
                                   nvinfer1::ITensor *const *inputs,
                                   int nbInputs,
                                   nvinfer1::INetworkDefinition *net,
                                   TensorMap *tensor_map,
                                   TensorModifyMap *tensor_modify_map) {
  std::shared_ptr<RPNProposalSSDPlugin> rpn_proposal_ssd_plugin;
  nvinfer1::Dims input_dims[3];
  for (int i = 0; i < nbInputs && i < 3; ++i) {
    input_dims[i] = inputs[i]->getDimensions();
  }
  // TODO(All): refactor it
  // convert proto message to pure struct to avoid adding protobuf as
  // cuda_library's dependence, which may case compile issue of nvcc
  StBBoxRegParameter bbox_reg_param;
  bbox_reg_param.bbox_mean.reserve(
      layer_param.bbox_reg_param().bbox_mean_size());
  for (int i = 0; i < layer_param.bbox_reg_param().bbox_mean_size(); ++i) {
    bbox_reg_param.bbox_mean[i] = layer_param.bbox_reg_param().bbox_mean(i);
  }
  bbox_reg_param.bbox_std.reserve(layer_param.bbox_reg_param().bbox_std_size());
  for (int i = 0; i < layer_param.bbox_reg_param().bbox_std_size(); ++i) {
    bbox_reg_param.bbox_std[i] = layer_param.bbox_reg_param().bbox_std(i);
  }
  StDetectionOutputSSDParameter detection_output_ssd_param;
  detection_output_ssd_param.heat_map_a =
      layer_param.detection_output_ssd_param().heat_map_a();
  detection_output_ssd_param.min_size_h =
      layer_param.detection_output_ssd_param().min_size_h();
  detection_output_ssd_param.min_size_w =
      layer_param.detection_output_ssd_param().min_size_w();
  detection_output_ssd_param.min_size_mode =
      layer_param.detection_output_ssd_param().min_size_mode();
  detection_output_ssd_param.threshold_objectness =
      layer_param.detection_output_ssd_param().threshold_objectness();
  detection_output_ssd_param.refine_out_of_map_bbox =
      layer_param.detection_output_ssd_param().refine_out_of_map_bbox();
  detection_output_ssd_param.num_class =
      layer_param.detection_output_ssd_param().num_class();
  detection_output_ssd_param.rpn_proposal_output_score =
      layer_param.detection_output_ssd_param().rpn_proposal_output_score();
  detection_output_ssd_param.regress_agnostic =
      layer_param.detection_output_ssd_param().regress_agnostic();

  detection_output_ssd_param.gen_anchor_param.anchor_width.reserve(
      layer_param.detection_output_ssd_param()
          .gen_anchor_param()
          .anchor_width_size());
  for (int i = 0; i < layer_param.detection_output_ssd_param()
                          .gen_anchor_param()
                          .anchor_width_size();
       ++i) {
    detection_output_ssd_param.gen_anchor_param.anchor_width[i] =
        layer_param.detection_output_ssd_param()
            .gen_anchor_param()
            .anchor_width(i);
  }
  detection_output_ssd_param.gen_anchor_param.anchor_height.reserve(
      layer_param.detection_output_ssd_param()
          .gen_anchor_param()
          .anchor_height_size());
  for (int i = 0; i < layer_param.detection_output_ssd_param()
                          .gen_anchor_param()
                          .anchor_height_size();
       ++i) {
    detection_output_ssd_param.gen_anchor_param.anchor_height[i] =
        layer_param.detection_output_ssd_param()
            .gen_anchor_param()
            .anchor_height(i);
  }
  detection_output_ssd_param.threshold.reserve(
      layer_param.detection_output_ssd_param().threshold_size());
  for (int i = 0; i < layer_param.detection_output_ssd_param().threshold_size();
       ++i) {
    detection_output_ssd_param.threshold[i] =
        layer_param.detection_output_ssd_param().threshold(i);
  }
  detection_output_ssd_param.nms_param.need_nms =
      layer_param.detection_output_ssd_param().nms_param().need_nms();
  detection_output_ssd_param.nms_param.overlap_ratio.reserve(
      layer_param.detection_output_ssd_param()
          .nms_param()
          .overlap_ratio_size());
  for (int i = 0; i < layer_param.detection_output_ssd_param()
                          .nms_param()
                          .overlap_ratio_size();
       ++i) {
    detection_output_ssd_param.nms_param.overlap_ratio[i] =
        layer_param.detection_output_ssd_param().nms_param().overlap_ratio(i);
  }
  detection_output_ssd_param.nms_param.top_n.reserve(
      layer_param.detection_output_ssd_param().nms_param().top_n_size());
  for (int i = 0;
       i < layer_param.detection_output_ssd_param().nms_param().top_n_size();
       ++i) {
    detection_output_ssd_param.nms_param.top_n[i] =
        layer_param.detection_output_ssd_param().nms_param().top_n(i);
  }
  detection_output_ssd_param.nms_param.max_candidate_n.reserve(
      layer_param.detection_output_ssd_param()
          .nms_param()
          .max_candidate_n_size());
  for (int i = 0; i < layer_param.detection_output_ssd_param()
                          .nms_param()
                          .max_candidate_n_size();
       ++i) {
    detection_output_ssd_param.nms_param.max_candidate_n[i] =
        layer_param.detection_output_ssd_param().nms_param().max_candidate_n(i);
  }
  detection_output_ssd_param.nms_param.use_soft_nms.reserve(
      layer_param.detection_output_ssd_param().nms_param().use_soft_nms_size());
  for (int i = 0;
       i <
       layer_param.detection_output_ssd_param().nms_param().use_soft_nms_size();
       ++i) {
    detection_output_ssd_param.nms_param.use_soft_nms[i] =
        layer_param.detection_output_ssd_param().nms_param().use_soft_nms(i);
  }
  detection_output_ssd_param.nms_param.voting.reserve(
      layer_param.detection_output_ssd_param().nms_param().voting_size());
  for (int i = 0;
       i < layer_param.detection_output_ssd_param().nms_param().voting_size();
       ++i) {
    detection_output_ssd_param.nms_param.voting[i] =
        layer_param.detection_output_ssd_param().nms_param().voting(i);
  }
  detection_output_ssd_param.nms_param.vote_iou.reserve(
      layer_param.detection_output_ssd_param().nms_param().vote_iou_size());
  for (int i = 0;
       i < layer_param.detection_output_ssd_param().nms_param().vote_iou_size();
       ++i) {
    detection_output_ssd_param.nms_param.vote_iou[i] =
        layer_param.detection_output_ssd_param().nms_param().vote_iou(i);
  }
  detection_output_ssd_param.nms_param.add_score =
      layer_param.detection_output_ssd_param().nms_param().add_score();
  detection_output_ssd_param.nms_param.nms_among_classes =
      layer_param.detection_output_ssd_param().nms_param().nms_among_classes();
  detection_output_ssd_param.nms_param.force_identity_iou_thr =
      layer_param.detection_output_ssd_param()
          .nms_param()
          .force_identity_iou_thr();
  detection_output_ssd_param.nms_param.force_imparity_iou_thr =
      layer_param.detection_output_ssd_param()
          .nms_param()
          .force_imparity_iou_thr();
  detection_output_ssd_param.nms_param.nms_gpu_max_n_per_time =
      layer_param.detection_output_ssd_param()
          .nms_param()
          .nms_gpu_max_n_per_time();

  rpn_proposal_ssd_plugin.reset(new RPNProposalSSDPlugin(
      bbox_reg_param, detection_output_ssd_param, input_dims));
  rpn_proposal_ssd_plugins_.push_back(rpn_proposal_ssd_plugin);
#ifdef NV_TENSORRT_MAJOR
#if NV_TENSORRT_MAJOR != 8
  nvinfer1::IPluginLayer *rpn_proposal_ssd_layer =
      net->addPlugin(inputs, nbInputs, *rpn_proposal_ssd_plugin);
#else
  nvinfer1::IPluginV2Layer *rpn_proposal_ssd_layer =
      net->addPluginV2(inputs, nbInputs, *rpn_proposal_ssd_plugin);
#endif
#endif
  rpn_proposal_ssd_layer->setName(layer_param.name().c_str());

  ConstructMap(layer_param, rpn_proposal_ssd_layer, tensor_map,
               tensor_modify_map);
}

void RTNet::addLayer(const LayerParameter &layer_param,
                     nvinfer1::ITensor *const *inputs, int nbInputs,
                     WeightMap *weight_map, nvinfer1::INetworkDefinition *net,
                     TensorMap *tensor_map,
                     TensorModifyMap *tensor_modify_map) {
  if (layer_param.type() == "Convolution") {
    addConvLayer(layer_param, inputs, weight_map, net, tensor_map,
                 tensor_modify_map);
  } else if (layer_param.type() == "Deconvolution") {
    addDeconvLayer(layer_param, inputs, weight_map, net, tensor_map,
                   tensor_modify_map);
  } else if (layer_param.type() == "Sigmoid" || layer_param.type() == "TanH" ||
             layer_param.type() == "ReLU") {
    addActiveLayer(layer_param, inputs, nbInputs, net, tensor_map,
                   tensor_modify_map);
  } else if (layer_param.type() == "Concat") {
    addConcatLayer(layer_param, inputs, nbInputs, net, tensor_map,
                   tensor_modify_map);
  } else if (layer_param.type() == "Slice") {
    addSliceLayer(layer_param, inputs, nbInputs, net, tensor_map,
                  tensor_modify_map);
  } else if (layer_param.type() == "Reshape") {
    addReshapeLayer(layer_param, inputs, net, tensor_map, tensor_modify_map);
  } else if (layer_param.type() == "Padding") {
    addPaddingLayer(layer_param, inputs, net, tensor_map, tensor_modify_map);
  } else if (layer_param.type() == "Pooling") {
    addPoolingLayer(layer_param, inputs, net, tensor_map, tensor_modify_map);
  } else if (layer_param.type() == "Permute") {
    addPermuteLayer(layer_param, inputs, nbInputs, net, tensor_map,
                    tensor_modify_map);
  } else if (layer_param.type() == "InnerProduct") {
    addInnerproductLayer(layer_param, inputs, weight_map, net, tensor_map,
                         tensor_modify_map);
  } else if (layer_param.type() == "BatchNorm") {
    addBatchnormLayer(layer_param, inputs, weight_map, net, tensor_map,
                      tensor_modify_map);
  } else if (layer_param.type() == "Scale") {
    addScaleLayer(layer_param, inputs, weight_map, net, tensor_map,
                  tensor_modify_map);
  } else if (layer_param.type() == "Softmax") {
    addSoftmaxLayer(layer_param, inputs, nbInputs, net, tensor_map,
                    tensor_modify_map);
  } else if (layer_param.type() == "Eltwise") {
    addEltwiseLayer(layer_param, inputs, net, tensor_map, tensor_modify_map);
  } else if (layer_param.type() == "ArgMax") {
    addArgmaxLayer(layer_param, inputs, nbInputs, net, tensor_map,
                   tensor_modify_map);
  } else if (layer_param.type() == "Dropout") {
    AINFO << "skip dropout";
  } else if (layer_param.type() == "Power") {
    addScaleLayer(layer_param, inputs, weight_map, net, tensor_map,
                  tensor_modify_map);
  } else if (layer_param.type() == "DFMBPSROIAlign") {
    addDFMBPSROIAlignLayer(layer_param, inputs, nbInputs, net, tensor_map,
                           tensor_modify_map);
  } else if (layer_param.type() == "RCNNProposal") {
    addRCNNProposalLayer(layer_param, inputs, nbInputs, net, tensor_map,
                         tensor_modify_map);
  } else if (layer_param.type() == "RPNProposalSSD") {
    addRPNProposalSSDLayer(layer_param, inputs, nbInputs, net, tensor_map,
                           tensor_modify_map);
  } else {
    AWARN << "unknown layer type:" << layer_param.type();
  }
}
bool RTNet::loadWeights(const std::string &model_file, WeightMap *weight_map) {
  NetParameter net;
  if (!ReadProtoFromBinaryFile(model_file.c_str(), &net)) {
    AFATAL << "open file " << model_file << " failed";
    return false;
  }
  for (int i = 0; i < net.layer_size(); i++) {
    std::vector<nvinfer1::Weights> lw;
    for (int j = 0; j < net.layer(i).blobs_size(); j++) {
      // val memory will be released when deconstructor is called
      auto blob = &(net.layer(i).blobs(j));
      CHECK_EQ(blob->double_data_size(), 0);
      CHECK_EQ(blob->double_diff_size(), 0);
      CHECK_EQ(blob->diff_size(), 0);
      CHECK_NE(blob->data_size(), 0);
      if (net.layer(i).type() == "BatchNorm") {
        mergeBN(j, net.mutable_layer(i));
      }
      auto wt = loadLayerWeights(blob->data().data(), blob->data_size());
      lw.push_back(wt);
    }
    (*weight_map)[net.layer(i).name().c_str()] = lw;
  }
  return true;
}
void RTNet::mergeBN(int index, LayerParameter *layer_param) {
  auto blob = (layer_param->mutable_blobs(index));
  CHECK_EQ(blob->double_data_size(), 0);
  CHECK_EQ(blob->double_diff_size(), 0);
  CHECK_EQ(blob->diff_size(), 0);
  CHECK_NE(blob->data_size(), 0);
  int size = blob->data_size();

  auto scale = (layer_param->blobs(2));
  const float scale_factor = scale.data(0) == 0 ? 0 : 1 / scale.data(0);
  auto epsilon = layer_param->batch_norm_param().eps();
  if (index == 0) {
    const BlobProto *var = (layer_param->mutable_blobs(index + 1));
    for (int k = 0; k < size; k++) {
      auto data = blob->data(k);
      blob->set_data(
          k, static_cast<float>(-data * scale_factor /
                                sqrt(var->data(k) * scale_factor + epsilon)));
    }
  } else if (index == 1) {
    for (int k = 0; k < size; k++) {
      blob->set_data(k, 1.0f / static_cast<float>(sqrt(
                                   blob->data(k) * scale_factor + epsilon)));
    }
  }
}
nvinfer1::Weights RTNet::loadLayerWeights(const float *data, int size) {
  nvinfer1::Weights wt{nvinfer1::DataType::kFLOAT, nullptr, 0};
  std::shared_ptr<float> val;
  val.reset(new float[size]);
  weights_mem_.push_back(val);
  for (int k = 0; k < size; k++) {
    val.get()[k] = data[k];
  }
  wt.values = val.get();
  wt.count = size;
  return wt;
}
nvinfer1::Weights RTNet::loadLayerWeights(float data, int size) {
  nvinfer1::Weights wt{nvinfer1::DataType::kFLOAT, nullptr, 0};
  std::shared_ptr<float> val;
  val.reset(new float[size]);
  weights_mem_.push_back(val);
  for (int k = 0; k < size; k++) {
    val.get()[k] = data;
  }
  wt.values = val.get();
  wt.count = size;
  return wt;
}

RTNet::RTNet(const std::string &net_file, const std::string &model_file,
             const std::vector<std::string> &outputs,
             const std::vector<std::string> &inputs)
    : output_names_(outputs), input_names_(inputs) {
  loadWeights(model_file, &weight_map_);
  net_param_.reset(new NetParameter);
  loadNetParams(net_file, net_param_.get());
}
RTNet::RTNet(const std::string &net_file, const std::string &model_file,
             const std::vector<std::string> &outputs,
             const std::vector<std::string> &inputs,
             nvinfer1::Int8EntropyCalibrator *calibrator)
    : output_names_(outputs), input_names_(inputs) {
  loadWeights(model_file, &weight_map_);
  net_param_.reset(new NetParameter);
  loadNetParams(net_file, net_param_.get());
  calibrator_ = calibrator;
  is_own_calibrator_ = false;
}
RTNet::RTNet(const std::string &net_file, const std::string &model_file,
             const std::vector<std::string> &outputs,
             const std::vector<std::string> &inputs,
             const std::string &model_root)
    : output_names_(outputs), input_names_(inputs), is_own_calibrator_(true) {
  loadWeights(model_file, &weight_map_);
  net_param_.reset(new NetParameter);
  loadNetParams(net_file, net_param_.get());
  model_root_ = model_root;
  BatchStream stream;

  calibrator_ =
      new nvinfer1::Int8EntropyCalibrator(stream, 0, true, model_root_);
}

bool RTNet::shape(const std::string &name, std::vector<int> *res) {
  auto engine = &(context_->getEngine());
  if (tensor_modify_map_.find(name) == tensor_modify_map_.end()) {
    AINFO << "can't get the shape of " << name;
    return false;
  }
  int bindingIndex = engine->getBindingIndex(tensor_modify_map_[name].c_str());
  if (bindingIndex > static_cast<int>(buffers_.size())) {
    return false;
  }
  nvinfer1::DimsCHW dims = static_cast<nvinfer1::DimsCHW &&>(
      engine->getBindingDimensions(bindingIndex));
  res->resize(4);
  (*res)[0] = max_batch_size_;
  (*res)[1] = dims.c();
  (*res)[2] = dims.h();
  (*res)[3] = dims.w();
  return true;
}
void RTNet::init_blob(std::vector<std::string> *names) {
  auto engine = &(context_->getEngine());

  for (auto name : *names) {
    int bindingIndex =
        engine->getBindingIndex(tensor_modify_map_[name].c_str());
    CHECK_LT(static_cast<size_t>(bindingIndex), buffers_.size());
    CHECK_GE(bindingIndex, 0);
    nvinfer1::DimsCHW dims = static_cast<nvinfer1::DimsCHW &&>(
        engine->getBindingDimensions(bindingIndex));
    int count = dims.c() * dims.h() * dims.w() * max_batch_size_;
    cudaMalloc(&buffers_[bindingIndex], count * sizeof(float));
    std::vector<int> shape;
    ACHECK(this->shape(name, &shape));
    std::shared_ptr<apollo::perception::base::Blob<float>> blob;
    blob.reset(new apollo::perception::base::Blob<float>(shape));
    blob->set_gpu_data(reinterpret_cast<float *>(buffers_[bindingIndex]));
    blobs_.insert(std::make_pair(name, blob));
  }
}
bool LoadCache(const std::string &path) {
  // auto hash_path = path + ".hash";
  struct stat buffer;
  if (stat(path.c_str(), &buffer) != 0) {
    AERROR << "[INFO] cannot find model cache : " << path
           << ", it will take minutes to generate...";
    return false;
  }
  return true;
}
bool RTNet::Init(const std::map<std::string, std::vector<int>> &shapes) {
  if (gpu_id_ < 0) {
    AINFO << "must use gpu mode";
    return false;
  }
  BASE_CUDA_CHECK(cudaSetDevice(gpu_id_));
  // stream will only be destoried for gpu_id_ >= 0
  cudaStreamCreate(&stream_);

  builder_ = nvinfer1::createInferBuilder(rt_gLogger);

  workspaceSize_ = 1 << 30;
  cudaDeviceProp prop;
  cudaGetDeviceProperties(&prop, gpu_id_);
  bool int8_mode = checkInt8(prop.name, calibrator_);

#ifdef NV_TENSORRT_MAJOR
#if NV_TENSORRT_MAJOR != 8
  network_ = builder_->createNetwork();

  parse_with_api(shapes);
  builder_->setMaxBatchSize(max_batch_size_);
  builder_->setMaxWorkspaceSize(workspaceSize_);

  builder_->setInt8Mode(int8_mode);
  builder_->setInt8Calibrator(calibrator_);

  builder_->setDebugSync(true);

  nvinfer1::ICudaEngine *engine = builder_->buildCudaEngine(*network_);
#else
  network_ = builder_->createNetworkV2(0U);
  parse_with_api(shapes);
  builder_config_ = builder_->createBuilderConfig();
  builder_->setMaxBatchSize(max_batch_size_);
  builder_config_->setMaxWorkspaceSize(workspaceSize_);

  if (int8_mode) {
    builder_config_->setFlag(nvinfer1::BuilderFlag::kINT8);
  } else {
    builder_config_->setFlag(nvinfer1::BuilderFlag::kDEBUG);
  }
  builder_config_->setInt8Calibrator(calibrator_);

  // serialize trt engine the first time
  nvinfer1::ICudaEngine *engine = nullptr;
  auto trt_cache_path = model_root_ + "/TRTengine.cache";
  if (!LoadCache(trt_cache_path)) {
    engine = builder_->buildEngineWithConfig(*network_, *builder_config_);

    if (engine->serialize()) {
      nvinfer1::IHostMemory *serialized_model(engine->serialize());
      std::ofstream f(trt_cache_path, std::ios::binary);

      f.write(reinterpret_cast<char *>(serialized_model->data()),
              serialized_model->size());
      serialized_model->destroy();
      AERROR << "Saving serialized model file to " << trt_cache_path;
    }
  } else {
    AINFO << "Loading TensorRT engine from serialized model file...";
    std::ifstream planFile(trt_cache_path);

    if (!planFile.is_open()) {
      AERROR << "Could not open serialized model";
      return false;
    }
    initLibNvInferPlugins(&rt_gLogger, "");

    AINFO << "success open serialized model";
    std::stringstream planBuffer;
    planBuffer << planFile.rdbuf();
    std::string plan = planBuffer.str();
    nvinfer1::IRuntime *runtime = nvinfer1::createInferRuntime(rt_gLogger);

    engine = runtime->deserializeCudaEngine(
        static_cast<const void *>(plan.data()), plan.size(), nullptr);
    runtime->destroy();
  }
#endif
#endif
  context_ = engine->createExecutionContext();
  buffers_.resize(input_names_.size() + output_names_.size());
  init_blob(&input_names_);
  init_blob(&output_names_);
  AINFO << "engine init success";
  return true;
}
bool RTNet::checkInt8(const std::string &gpu_name,
                      nvinfer1::IInt8Calibrator *calibrator) {
  if (calibrator == nullptr) {
    AINFO << "Device Works on FP32 Mode.";
    return false;
  }
  for (auto ref : _gpu_checklist) {
    if (ref == gpu_name) {
      AINFO << "Device Works on Int8 Mode.";
      return true;
    }
  }
  AWARN << "Device Not Supports Int8 Mode. Use FP32 Mode.";
  calibrator_ = nullptr;
  return false;
}
bool RTNet::addInput(const TensorDimsMap &tensor_dims_map,
                     const std::map<std::string, std::vector<int>> &shapes,
                     TensorMap *tensor_map) {
  CHECK_GT(net_param_->layer_size(), 0);
  input_names_.clear();
  for (auto dims_pair : tensor_dims_map) {
    if (shapes.find(dims_pair.first) != shapes.end()) {
      auto shape = shapes.at(dims_pair.first);
      if (static_cast<int>(shape.size()) == dims_pair.second.nbDims + 1) {
        max_batch_size_ = std::max(max_batch_size_, shape[0]);
        for (int i = 0; i < dims_pair.second.nbDims; i++) {
          dims_pair.second.d[i] = shape[i + 1];
        }
      }
    }
    auto data = network_->addInput(
        dims_pair.first.c_str(), nvinfer1::DataType::kFLOAT, dims_pair.second);
    CHECK_NOTNULL(data);
    data->setName(dims_pair.first.c_str());
    tensor_map->insert(std::make_pair(dims_pair.first, data));
    input_names_.push_back(dims_pair.first);
  }
  return true;
}

void RTNet::parse_with_api(
    const std::map<std::string, std::vector<int>> &shapes) {
  CHECK_GT(net_param_->layer_size(), 0);
  std::vector<LayerParameter> order;
  TensorMap tensor_map;
  TensorDimsMap tensor_dims_map;
  ParseNetParam(*net_param_, &tensor_dims_map, &tensor_modify_map_, &order);
  addInput(tensor_dims_map, shapes, &tensor_map);
  for (auto layer_param : order) {
    std::vector<nvinfer1::ITensor *> inputs;
    for (int j = 0; j < layer_param.bottom_size(); j++) {
      CHECK_NOTNULL(tensor_map[tensor_modify_map_[layer_param.bottom(j)]]);
      inputs.push_back(tensor_map[tensor_modify_map_[layer_param.bottom(j)]]);
    }
    addLayer(layer_param, inputs.data(), layer_param.bottom_size(),
             &weight_map_, network_, &tensor_map, &tensor_modify_map_);
  }

  CHECK_NE(output_names_.size(), static_cast<size_t>(0));
  std::sort(output_names_.begin(), output_names_.end());
  auto last = std::unique(output_names_.begin(), output_names_.end());
  output_names_.erase(last, output_names_.end());
  for (auto iter = output_names_.begin(); iter != output_names_.end();) {
    if (tensor_map.find(tensor_modify_map_[*iter]) != tensor_map.end()) {
      network_->markOutput(*tensor_map[tensor_modify_map_[*iter]]);
      iter++;
    } else {
      AINFO << "Erase output: " << *iter;
      iter = output_names_.erase(iter);
    }
  }
}

RTNet::~RTNet() {
  if (is_own_calibrator_ && calibrator_ != nullptr) {
    delete calibrator_;
  }
  if (gpu_id_ >= 0) {
    BASE_CUDA_CHECK(cudaStreamDestroy(stream_));
    network_->destroy();
#ifdef NV_TENSORRT_MAJOR
#if NV_TENSORRT_MAJOR != 8
    builder_config_->destroy();
#endif
#endif
    context_->destroy();
    for (auto buf : buffers_) {
      cudaFree(buf);
    }
  }
}

void RTNet::Infer() {
  BASE_CUDA_CHECK(cudaSetDevice(gpu_id_));
  BASE_CUDA_CHECK(cudaStreamSynchronize(stream_));
  for (auto name : input_names_) {
    auto blob = get_blob(name);
    if (blob != nullptr) {
      blob->gpu_data();
    }
  }
  // If `out_blob->mutable_cpu_data()` is invoked outside,
  // HEAD will be set to CPU, and `out_blob->mutable_gpu_data()`
  // after `enqueue` will copy data from CPU to GPU,
  // which will overwrite the `inference` results.
  // `out_blob->gpu_data()` will set HEAD to SYNCED,
  // then no copy happends after `enqueue`.
  for (auto name : output_names_) {
    auto blob = get_blob(name);
    if (blob != nullptr) {
      blob->gpu_data();
    }
  }
  context_->enqueue(max_batch_size_, &buffers_[0], stream_, nullptr);
  BASE_CUDA_CHECK(cudaStreamSynchronize(stream_));

  for (auto name : output_names_) {
    auto blob = get_blob(name);
    if (blob != nullptr) {
      blob->mutable_gpu_data();
    }
  }
}
std::shared_ptr<apollo::perception::base::Blob<float>> RTNet::get_blob(
    const std::string &name) {
  auto iter = blobs_.find(name);
  if (iter == blobs_.end()) {
    return nullptr;
  }
  return iter->second;
}

}  // namespace inference
}  // namespace perception
}  // namespace apollo
