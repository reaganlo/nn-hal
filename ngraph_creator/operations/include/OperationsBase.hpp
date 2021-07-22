#pragma once

#include <Driver.h>
#include <android/log.h>
#include <log/log.h>
#include <NgraphHelper.hpp>
#include <NgraphNodes.hpp>
#include <ngraph/ngraph.hpp>
#include <ngraph/opsets/opset3.hpp>

#include "ModelManager.h"

namespace android {
namespace hardware {
namespace neuralnetworks {
namespace nnhal {

class OperationsBase {
protected:
    enum ConversionType { NHWC_NCHW, NCHW_NHWC, IHWO_OIHW, OHWI_OIHW, NHC_NCH, NCH_NHC, NC_CN };
    uint32_t mDefaultOutputIndex;
    uint32_t mDefaultInputIndex = 0;
    int mNnapiOperationIndex;
    std::shared_ptr<ngraph::Node> transpose(ConversionType type,
                                            ngraph::Output<ngraph::Node> input);
    virtual std::shared_ptr<ngraph::Node> createNode() = 0;
    // override createNodeForPlugin in case sPluginType specific implementation is required
    virtual std::shared_ptr<ngraph::Node> createNodeForPlugin();
    std::shared_ptr<ngraph::Node> toNCHW(size_t inputIndex, size_t outputIndex);
    void addResultNode(size_t index, std::shared_ptr<ngraph::Node> resultNode);

    // helper functions
    bool checkOperandType(uint32_t operandIndex, const int32_t expectedOperandType,
                          const std::string& strLogInfo = "Operand");
    bool checkOutputOperandType(uint32_t index, const int32_t expectedOperandType);
    bool checkInputOperandType(uint32_t index, const int32_t expectedOperandType);
    const vec<uint32_t> getInputOperandDimensions(uint32_t inputIndex);
    bool isValidInputTensor(uint32_t inputIndex);

    template<typename T>
    bool deQuantize(const T* inputData, const uint32_t& len, const float scale,
                const int32_t zeroPoint, float* outputData) {
        int32_t value;
        for (int i = 0; i < len; ++i) {
            value = *(inputData + i);
            outputData[i] = static_cast<float>(scale * (value - zeroPoint));
        }
    return true;
    }

    std::shared_ptr<ngraph::Node> getInputNode(uint32_t inputIndex, bool dequantize = true) {
        std::shared_ptr<ngraph::Node> input;
        auto operandIndex = sModelInfo->getOperationInput(mNnapiOperationIndex, inputIndex);
        auto operandType = sModelInfo->getOperandType(operandIndex);
        float scale;
	int32_t zp;
        if (sModelInfo->isOperandLifeTimeConst(operandIndex)) {
            auto operandDims = getInputOperandDimensions(inputIndex);
            ngraph::element::Type elementType;
            switch (operandType) {
                case OperandType::TENSOR_FLOAT32: {
                    elementType = ngraph::element::f32;
                    auto operandValues = sModelInfo->GetConstVecOperand<float>(operandIndex);
                    input = createConstNode(elementType, toNgraphShape(operandDims), operandValues);
                    break;
                }
                case OperandType::TENSOR_INT32: {
                    sModelInfo->getOperandScaleZeroPoint(operandIndex, scale, zp);
                    elementType = ngraph::element::f32;
                    auto operandValues = sModelInfo->GetConstVecOperand<int>(operandIndex);
                    std::vector<float> f_operandValues;
                    f_operandValues.resize(operandValues.size());
                    deQuantize(operandValues.data(), operandValues.size(), scale, zp, f_operandValues.data());
                    input = createConstNode(elementType, toNgraphShape(operandDims), operandValues);
                    break;
                }
                case OperandType::TENSOR_BOOL8: {
                    elementType = ngraph::element::boolean;
                    auto operandValues = sModelInfo->GetConstVecOperand<uint8_t>(operandIndex);
                    input = createConstNode(elementType, toNgraphShape(operandDims), operandValues);
                    break;
                }
                case OperandType::TENSOR_QUANT8_ASYMM: {
                    elementType = ngraph::element::u8;
                    auto operandValues = sModelInfo->GetConstVecOperand<uint8_t>(operandIndex);
                    input = createConstNode(elementType, toNgraphShape(operandDims), operandValues);
                    break;
                }
                case OperandType::TENSOR_QUANT8_ASYMM_SIGNED: {
                    sModelInfo->getOperandScaleZeroPoint(operandIndex, scale, zp);
                    elementType = ngraph::element::i8;
                    auto operandValues = sModelInfo->GetConstVecOperand<int8_t>(operandIndex);
                    input = createConstNode(elementType, toNgraphShape(operandDims), operandValues);
                    break;
                }
                case OperandType::TENSOR_QUANT16_SYMM: {
                    elementType = ngraph::element::f32;
                    sModelInfo->getOperandScaleZeroPoint(operandIndex, scale, zp);
                    auto operandValues = sModelInfo->GetConstVecOperand<int16_t>(operandIndex);
                    std::vector<float> f_operandValues;
                    f_operandValues.resize(operandValues.size());
                    deQuantize(operandValues.data(), operandValues.size(), scale, zp, f_operandValues.data());
                    input = createConstNode(elementType, toNgraphShape(operandDims), f_operandValues);
                    break;
                }
                case OperandType::TENSOR_QUANT8_SYMM: {
                    elementType = ngraph::element::f32;
                    sModelInfo->getOperandScaleZeroPoint(operandIndex, scale, zp);
                    auto operandValues = sModelInfo->GetConstVecOperand<int8_t>(operandIndex);
                    std::vector<float> f_operandValues;
                    f_operandValues.resize(operandValues.size());
                    deQuantize(operandValues.data(), operandValues.size(), scale, zp, f_operandValues.data());
                    input = createConstNode(elementType, toNgraphShape(operandDims), f_operandValues);


                    break;
                }
                default: {
                    ALOGE("Unsupported Tensor type %s inputIndex %d, operandType %d", __func__,
                          inputIndex, operandType);
                    return nullptr;
                }
            }

        } else {
            input = mNgraphNodes->getOperationOutput(operandIndex).get_node_shared_ptr();
        }

        return input;
    }
    // remove null input node parameter
    void removeInputNode(uint32_t inputIndex) {
        auto operandIndex = sModelInfo->getOperationInput(mNnapiOperationIndex, inputIndex);
        auto nodeName = mNgraphNodes->getNodeName(operandIndex);
        mNgraphNodes->removeInputParameter(nodeName, operandIndex);
    }

    template <typename T>
    std::shared_ptr<ngraph::Node> createConstNode(ngraph::element::Type elementType,
                                                  ngraph::Shape shape, std::vector<T> vals) {
        return ngraph::op::Constant::create(elementType, shape, vals);
    }

    template <typename T>
    std::vector<T> convertToVector(T val) {
        std::vector<T> vec;
        vec.push_back(val);

        return vec;
    }

    std::shared_ptr<ngraph::Node> QuantizeNode(std::shared_ptr<ngraph::Node> input, size_t index,
                                               ngraph::element::Type quantizeType);
    std::shared_ptr<ngraph::Node> DequantizeNode(std::shared_ptr<ngraph::Node> input, size_t index,
                                                 ngraph::element::Type dequantizeType);

public:
    static std::shared_ptr<NnapiModelInfo> sModelInfo;
    static std::string sPluginType;
    std::shared_ptr<NgraphNodes> mNgraphNodes;
    OperationsBase(int operationIndex);
    void setNgraphNodes(std::shared_ptr<NgraphNodes> nodes);
    virtual bool validate();
    // override validateForPlugin in case sPluginType specific implementation is required
    virtual bool validateForPlugin();
    // override connectOperationToGraph in case Operation has multiple outputs
    virtual void connectOperationToGraph();
};

}  // namespace nnhal
}  // namespace neuralnetworks
}  // namespace hardware
}  // namespace android
