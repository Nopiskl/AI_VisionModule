#include "NeuralNetworkRuntime.hpp"

#include <iostream>
#include <fstream>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

#include "vip_lite.h"

#include "VipStatusException.hpp"


class NeuralNetworkRuntime::Impl
{
private:
    enum BufferType
    {
        TYPE_UNDEFINED,
        TYPE_IN,
        TYPE_OUT
    };

    NeuralNetworkRuntime::Config config;

    vip_network network = nullptr;
    bool vipInitialized = false;

    std::vector<vip_buffer_create_params_t> inputBufferParameters;
    std::vector<vip_buffer> inputBuffers;

    std::vector<vip_buffer_create_params_t> outputBufferParameters;
    std::vector<vip_buffer> outputBuffers;

    void queryBufferParameter(int index, vip_buffer_create_params_t &bufferCreateParams, BufferType type)
    {
        vip_status_e (*vipQueryBufferProp)(vip_network, vip_uint32_t, vip_enum, void *);

        if (type == BufferType::TYPE_IN)
        {
            vipQueryBufferProp = vip_query_input;
        }
        else if (type == BufferType::TYPE_OUT)
        {
            vipQueryBufferProp = vip_query_output;
        }
        else
        {
            throw std::invalid_argument("Invalid BufferType!");
        }

        memset(&bufferCreateParams, 0, sizeof(vip_buffer_create_params_t));

        vip_status_e status = vipQueryBufferProp(network, index, VIP_BUFFER_PROP_DATA_FORMAT, &bufferCreateParams.data_format);
        CHECK_VIP_STATUS(status);

        status = vipQueryBufferProp(network, index, VIP_BUFFER_PROP_NUM_OF_DIMENSION, &bufferCreateParams.num_of_dims);
        CHECK_VIP_STATUS(status);

        status = vipQueryBufferProp(network, index, VIP_BUFFER_PROP_SIZES_OF_DIMENSION, &bufferCreateParams.sizes);
        CHECK_VIP_STATUS(status);

        status = vipQueryBufferProp(network, index, VIP_BUFFER_PROP_QUANT_FORMAT, &bufferCreateParams.quant_format);
        CHECK_VIP_STATUS(status);

        switch (bufferCreateParams.quant_format)
        {
        case VIP_BUFFER_QUANTIZE_DYNAMIC_FIXED_POINT:
            status = vipQueryBufferProp(network, index, VIP_BUFFER_PROP_FIXED_POINT_POS, &bufferCreateParams.quant_data.dfp.fixed_point_pos);
            CHECK_VIP_STATUS(status);
            break;
        case VIP_BUFFER_QUANTIZE_TF_ASYMM:
            status = vipQueryBufferProp(network, index, VIP_BUFFER_PROP_TF_SCALE, &bufferCreateParams.quant_data.affine.scale);
            CHECK_VIP_STATUS(status);

            status = vipQueryBufferProp(network, index, VIP_BUFFER_PROP_TF_ZERO_POINT, &bufferCreateParams.quant_data.affine.zeroPoint);
            CHECK_VIP_STATUS(status);
            break;
        case VIP_BUFFER_QUANTIZE_NONE:
        default:
            break;
        }
    }

    void createNeuralNetworkBuffers()
    {
        vip_uint32_t inputCount;
        vip_status_e status = vip_query_network(network, VIP_NETWORK_PROP_INPUT_COUNT, &inputCount);
        CHECK_VIP_STATUS(status);

        inputBufferParameters.reserve(inputCount);
        inputBuffers.reserve(inputCount);
        for (vip_uint32_t i = 0; i < inputCount; i++)
        {
            vip_buffer_create_params_t bufferCreateParams;
            queryBufferParameter(i, bufferCreateParams, BufferType::TYPE_IN);

            inputBufferParameters.push_back(bufferCreateParams);

            vip_buffer inputBuffer;
            status = vip_create_buffer(&bufferCreateParams, sizeof(bufferCreateParams), &inputBuffer);
            CHECK_VIP_STATUS(status);

            inputBuffers.push_back(inputBuffer);
        }

        vip_uint32_t outputCount;
        status = vip_query_network(network, VIP_NETWORK_PROP_OUTPUT_COUNT, &outputCount);
        CHECK_VIP_STATUS(status);

        outputBufferParameters.reserve(outputCount);
        outputBuffers.reserve(outputCount);
        for (vip_uint32_t i = 0; i < outputCount; i++)
        {
            vip_buffer_create_params_t bufferCreateParams;
            queryBufferParameter(i, bufferCreateParams, BufferType::TYPE_OUT);

            outputBufferParameters.push_back(bufferCreateParams);

            vip_buffer outputBuffer;
            status = vip_create_buffer(&bufferCreateParams, sizeof(bufferCreateParams), &outputBuffer);
            CHECK_VIP_STATUS(status);

            outputBuffers.push_back(outputBuffer);
        }
    }

    inline NeuralNetworkRuntime::InputDataFormat mapToInputDataFormat(vip_enum dataFormat)
    {
        NeuralNetworkRuntime::InputDataFormat format;

        switch (dataFormat)
        {
            case VIP_BUFFER_FORMAT_FP32:
                format = NeuralNetworkRuntime::InputDataFormat::FORMAT_FP32;
                break;
            case VIP_BUFFER_FORMAT_FP16:
                format = NeuralNetworkRuntime::InputDataFormat::FORMAT_FP16;
                break;
            case VIP_BUFFER_FORMAT_UINT8:
                format = NeuralNetworkRuntime::InputDataFormat::FORMAT_UINT8;
                break;
            case VIP_BUFFER_FORMAT_INT8:
                format = NeuralNetworkRuntime::InputDataFormat::FORMAT_INT8;
                break;
            case VIP_BUFFER_FORMAT_UINT16:
                format = NeuralNetworkRuntime::InputDataFormat::FORMAT_UINT16;
                break;
            case VIP_BUFFER_FORMAT_INT16:
                format = NeuralNetworkRuntime::InputDataFormat::FORMAT_INT16;
                break;
            default:
                format = NeuralNetworkRuntime::InputDataFormat::FORMAT_UNKNOWN;
                break;
        }

        return format;
    }

    void loadInputData(const NeuralNetworkRuntime::LoadingInputDataCallback& onLoadingInputData)
    {
        for (std::size_t i = 0; i < inputBuffers.size(); ++i)
        {
            void *buffer = vip_map_buffer(inputBuffers[i]);
            if (buffer == nullptr)
            {
                throw std::runtime_error("VIPLite failed to map an input buffer");
            }

            vip_buffer_create_params_t &bufferCreateParams = inputBufferParameters[i];

            NeuralNetworkRuntime::InputDataFormat elementDataFormat = mapToInputDataFormat(bufferCreateParams.data_format);
            try
            {
                onLoadingInputData(static_cast<int>(i), buffer, elementDataFormat,
                                   getBufferByteSize(bufferCreateParams));
            }
            catch (...)
            {
                vip_unmap_buffer(inputBuffers[i]);
                throw;
            }

            vip_status_e status = vip_unmap_buffer(inputBuffers[i]);
            CHECK_VIP_STATUS(status);
        }
    }

    vip_uint32_t getFormatBytes(const vip_enum type)
    {
        switch (type)
        {
        case VIP_BUFFER_FORMAT_INT8:
        case VIP_BUFFER_FORMAT_UINT8:
            return 1;
        case VIP_BUFFER_FORMAT_INT16:
        case VIP_BUFFER_FORMAT_UINT16:
        case VIP_BUFFER_FORMAT_FP16:
        case VIP_BUFFER_FORMAT_BFP16:
            return 2;
        case VIP_BUFFER_FORMAT_FP32:
        case VIP_BUFFER_FORMAT_INT32:
        case VIP_BUFFER_FORMAT_UINT32:
            return 4;
        case VIP_BUFFER_FORMAT_FP64:
        case VIP_BUFFER_FORMAT_INT64:
        case VIP_BUFFER_FORMAT_UINT64:
            return 8;

        default:
            return 0;
        }
    }

    std::size_t getElementCount(const vip_buffer_create_params_t &parameters)
    {
        if (parameters.num_of_dims == 0 || parameters.num_of_dims > 6)
        {
            throw std::runtime_error("VIPLite tensor has an invalid dimension count");
        }
        std::size_t count = 1;
        for (vip_uint32_t dimension = 0;
             dimension < parameters.num_of_dims; ++dimension)
        {
            const std::size_t size = parameters.sizes[dimension];
            if (size == 0 || count > std::numeric_limits<std::size_t>::max() / size)
            {
                throw std::runtime_error("VIPLite tensor size is invalid");
            }
            count *= size;
        }
        return count;
    }

    std::size_t getBufferByteSize(const vip_buffer_create_params_t &parameters)
    {
        const std::size_t formatBytes = getFormatBytes(parameters.data_format);
        const std::size_t elementCount = getElementCount(parameters);
        if (formatBytes == 0 ||
            elementCount > std::numeric_limits<std::size_t>::max() / formatBytes)
        {
            throw std::runtime_error("VIPLite tensor data format is unsupported");
        }
        return elementCount * formatBytes;
    }

    std::vector<std::vector<float>> collectResults()
    {
        std::vector<std::vector<float>> results(outputBuffers.size());

        for (std::size_t i = 0; i < outputBuffers.size(); ++i)
        {
            const vip_buffer_create_params_t &parameters =
                outputBufferParameters[i];
            if (parameters.data_format != VIP_BUFFER_FORMAT_INT16 ||
                parameters.quant_format !=
                    VIP_BUFFER_QUANTIZE_DYNAMIC_FIXED_POINT)
            {
                throw std::runtime_error(
                    "YOLO output tensor must use INT16 dynamic fixed point");
            }

            const std::size_t elementCount = getElementCount(parameters);
            const vip_int32_t fixedPointPosition =
                parameters.quant_data.dfp.fixed_point_pos;
            if (fixedPointPosition < -126 || fixedPointPosition > 126)
            {
                throw std::runtime_error(
                    "YOLO output tensor has an invalid fixed-point position");
            }

            const auto *buffer = static_cast<const vip_int16_t *>(
                vip_map_buffer(outputBuffers[i]));
            if (buffer == nullptr)
            {
                throw std::runtime_error("VIPLite failed to map an output buffer");
            }

            try
            {
                std::vector<float> result(elementCount);
                const float multiplier =
                    std::ldexp(1.0f, -fixedPointPosition);
                for (std::size_t element = 0; element < elementCount; ++element)
                {
                    result[element] =
                        static_cast<float>(buffer[element]) * multiplier;
                }
                results[i] = std::move(result);
            }
            catch (...)
            {
                vip_unmap_buffer(outputBuffers[i]);
                throw;
            }

            vip_status_e status = vip_unmap_buffer(outputBuffers[i]);
            CHECK_VIP_STATUS(status);
        }

        return results;
    }

public:
    Impl(Config &config)
        : config(config)
    {

    }

    Impl(const Impl &other) = delete;
    Impl &operator=(const Impl &other) = delete;

    Impl(Impl &&other) noexcept
        : config(std::move(other.config)),
          network(other.network),
          vipInitialized(other.vipInitialized),
          inputBufferParameters(std::move(other.inputBufferParameters)),
          inputBuffers(std::move(other.inputBuffers)),
          outputBufferParameters(std::move(other.outputBufferParameters)),
          outputBuffers(std::move(other.outputBuffers))
    {
        other.network = nullptr;
        other.vipInitialized = false;
    }

    Impl &operator=(Impl &&other) noexcept
    {
        if (this != &other)
        {
            destroy();
            config = std::move(other.config);
            network = other.network;
            vipInitialized = other.vipInitialized;
            inputBufferParameters = std::move(other.inputBufferParameters);
            inputBuffers = std::move(other.inputBuffers);
            outputBufferParameters = std::move(other.outputBufferParameters);
            outputBuffers = std::move(other.outputBuffers);

            other.network = nullptr;
            other.vipInitialized = false;
        }
        return *this;
    }

    void create()
    {
        std::cout << "NeuralNetworkRuntime Impl create..." << std::endl;

        //TODO thread-safe
        if (network != nullptr)
        {
            return;
        }

        try {
            vip_status_e status = vip_init(config.memSize);
            CHECK_VIP_STATUS(status);
            vipInitialized = true;

            std::ifstream model(config.modelFilePath,
                                std::ios::binary | std::ios::ate);
            if (!model.is_open())
            {
                throw std::runtime_error("Can't open network binary: " +
                                         config.modelFilePath);
            }
            const std::streamoff modelSize = model.tellg();
            if (modelSize <= 0 ||
                static_cast<unsigned long long>(modelSize) >
                    std::numeric_limits<vip_uint32_t>::max())
            {
                throw std::runtime_error("Network binary size is invalid");
            }
            std::vector<unsigned char> networkBuffer(
                static_cast<std::size_t>(modelSize));
            model.seekg(0, std::ios::beg);
            if (!model.read(reinterpret_cast<char *>(networkBuffer.data()),
                            modelSize))
            {
                throw std::runtime_error("Can't read network binary: " +
                                         config.modelFilePath);
            }
            status = vip_create_network(
                networkBuffer.data(), static_cast<vip_uint32_t>(modelSize),
                VIP_CREATE_NETWORK_FROM_MEMORY, &network);
            CHECK_VIP_STATUS(status);

            createNeuralNetworkBuffers();

            std::cout << "vip_prepare_network start..." << std::endl;
            status = vip_prepare_network(network);
            std::cout << "vip_prepare_network finish" << std::endl;
            CHECK_VIP_STATUS(status);
        }
        catch (...)
        {
            destroy();
            throw;
        }
    }

    std::vector<std::vector<float>> run(const NeuralNetworkRuntime::LoadingInputDataCallback& onLoadingInputData)
    {
        vip_status_e status = VIP_SUCCESS;

        loadInputData(onLoadingInputData);

        for (std::size_t i = 0; i < inputBuffers.size(); ++i)
        {
            status = vip_set_input(network, static_cast<vip_uint32_t>(i),
                                   inputBuffers[i]);
            CHECK_VIP_STATUS(status);
        }

        for (std::size_t i = 0; i < outputBuffers.size(); ++i)
        {
            status = vip_set_output(network, static_cast<vip_uint32_t>(i),
                                    outputBuffers[i]);
            CHECK_VIP_STATUS(status);
        }

        for (std::size_t i = 0; i < inputBuffers.size(); ++i)
        {
            status = vip_flush_buffer(inputBuffers[i], VIP_BUFFER_OPER_TYPE_FLUSH);
            CHECK_VIP_STATUS(status);
        }

        status = vip_run_network(network);
        CHECK_VIP_STATUS(status);

        for (std::size_t i = 0; i < outputBuffers.size(); ++i)
        {
            status = vip_flush_buffer(outputBuffers[i], VIP_BUFFER_OPER_TYPE_INVALIDATE);
            CHECK_VIP_STATUS(status);
        }

        // vip_inference_profile_t inferenceProfile;
        // status = vip_query_network(network, VIP_NETWORK_PROP_PROFILING, &inferenceProfile);
        // CHECK_VIP_STATUS(status);
        // std::cout << "-----inferenceTime = " << inferenceProfile.inference_time << std::endl;

        return collectResults();
    }

    void destroy()
    {
        if (network != nullptr)
        {
            vip_finish_network(network);
            vip_destroy_network(network);
            network = nullptr;
        }
        for (vip_buffer &buffer : inputBuffers)
        {
            if (buffer != nullptr)
            {
                vip_destroy_buffer(buffer);
                buffer = nullptr;
            }
        }
        for (vip_buffer &buffer : outputBuffers)
        {
            if (buffer != nullptr)
            {
                vip_destroy_buffer(buffer);
                buffer = nullptr;
            }
        }
        inputBuffers.clear();
        outputBuffers.clear();
        inputBufferParameters.clear();
        outputBufferParameters.clear();
        if (vipInitialized)
        {
            vip_destroy();
            vipInitialized = false;
        }
    }

    ~Impl()
    {
        destroy();
        std::cout << "NeuralNetworkRuntime destroyed!" << std::endl;
    }
};

NeuralNetworkRuntime::NeuralNetworkRuntime(Config &config)
    : _pImpl(new Impl(config))
{
    if (config.isAutoInit)
    {
        _pImpl->create();
    }
}

NeuralNetworkRuntime::NeuralNetworkRuntime(NeuralNetworkRuntime &&other) noexcept
    : _pImpl(std::move(other._pImpl))
{
    other._pImpl = nullptr;
}

NeuralNetworkRuntime &NeuralNetworkRuntime::operator=(NeuralNetworkRuntime &&other) noexcept
{
    if (this != &other)
    {
        _pImpl = std::move(other._pImpl);
        other._pImpl = nullptr;
    }
    return *this;
}

NeuralNetworkRuntime::~NeuralNetworkRuntime() = default;

void NeuralNetworkRuntime::create()
{
    _pImpl->create();
}

std::vector<std::vector<float>> NeuralNetworkRuntime::run(const LoadingInputDataCallback& onLoadingInputData)
{
    return _pImpl->run(onLoadingInputData);
}

void NeuralNetworkRuntime::destroy()
{
    _pImpl->destroy();
}
