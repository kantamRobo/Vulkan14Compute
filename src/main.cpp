#include <vulkan/vulkan.h>
#include <iostream>
#include <vector>
#include <fstream>
#include <cstring>
#include <stdexcept>
#include <iomanip>
#include <filesystem>

// 送信する定数データ (Push Constants)
struct PushConstants {
    int32_t multiplier;
    int32_t offset;
};

// 処理する要素数 (コンピュートシェーダーの local_size_x と一致)
constexpr uint32_t NUM_ELEMENTS = 64;

// SPIR-V バイナリファイルの読み込み
std::vector<char> readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("シェーダーファイルを開けませんでした: " + filename);
    }

    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

    return buffer;
}

// 適切なメモリタイプインデックスを検索
uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("適切なメモリタイプが見つかりませんでした。");
}

int main(int argc, char* argv[]) {
    std::cout << "====================================================\n";
    std::cout << " Vulkan 1.4 Compute Shader Demo\n";
    std::cout << "====================================================\n\n";

    VkInstance instance = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkQueue computeQueue = VK_NULL_HANDLE;
    uint32_t computeQueueFamilyIndex = 0;

    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory bufferMemory = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkShaderModule computeShaderModule = VK_NULL_HANDLE;
    VkPipeline computePipeline = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;

    try {
        // -------------------------------------------------------------
        // 0. シェーダーバイナリの探索
        // -------------------------------------------------------------
        std::vector<std::filesystem::path> candidates;
        if (argc > 0) {
            std::filesystem::path exeDir = std::filesystem::absolute(argv[0]).parent_path();
            candidates.push_back(exeDir / "compute.spv");
            candidates.push_back(exeDir / "shaders" / "compute.spv");
            candidates.push_back(exeDir / ".." / "compute.spv");
        }
        candidates.push_back("compute.spv");
        candidates.push_back("build/compute.spv");
        candidates.push_back("build/Release/compute.spv");
        candidates.push_back("../build/Release/compute.spv");
        candidates.push_back("shaders/compute.spv");
        candidates.push_back("../shaders/compute.spv");

        std::string shaderPath = "";
        for (const auto& path : candidates) {
            if (std::filesystem::exists(path)) {
                shaderPath = path.string();
                break;
            }
        }

        if (shaderPath.empty()) {
            throw std::runtime_error("compute.spv が見つかりませんでした。ビルドが正しく行われているか確認してください。");
        }
        std::cout << "[INFO] シェーダーバイナリを発見しました: " << shaderPath << "\n";
        auto shaderCode = readFile(shaderPath);

        // -------------------------------------------------------------
        // 1. Vulkan 1.4 インスタンスの作成
        // -------------------------------------------------------------
        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "Vulkan 1.4 Compute Demo";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "No Engine";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        // Vulkan 1.4 を明示的に指定
        appInfo.apiVersion = VK_API_VERSION_1_4;

        VkInstanceCreateInfo instanceCreateInfo{};
        instanceCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instanceCreateInfo.pApplicationInfo = &appInfo;

        // Validation Layerの確認と設定（利用可能な場合）
        const char* validationLayerName = "VK_LAYER_KHRONOS_validation";
        uint32_t layerCount = 0;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
        std::vector<VkLayerProperties> availableLayers(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

        bool validationSupported = false;
        for (const auto& layer : availableLayers) {
            if (strcmp(layer.layerName, validationLayerName) == 0) {
                validationSupported = true;
                break;
            }
        }

        if (validationSupported) {
            instanceCreateInfo.enabledLayerCount = 1;
            instanceCreateInfo.ppEnabledLayerNames = &validationLayerName;
            std::cout << "[INFO] Validation Layer (" << validationLayerName << ") を有効化しました。\n";
        }

        if (vkCreateInstance(&instanceCreateInfo, nullptr, &instance) != VK_SUCCESS) {
            throw std::runtime_error("Vulkan 1.4 インスタンスの作成に失敗しました。");
        }
        std::cout << "[INFO] Vulkan 1.4 インスタンスを作成しました。\n";

        // -------------------------------------------------------------
        // 2. 物理デバイスの選択
        // -------------------------------------------------------------
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
        if (deviceCount == 0) {
            throw std::runtime_error("Vulkanをサポートする物理デバイスが見つかりません。");
        }

        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

        // コンピュートキューをサポートするGPUを検索
        for (const auto& dev : devices) {
            uint32_t queueFamilyCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(dev, &queueFamilyCount, nullptr);
            std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
            vkGetPhysicalDeviceQueueFamilyProperties(dev, &queueFamilyCount, queueFamilies.data());

            for (uint32_t i = 0; i < queueFamilyCount; i++) {
                if (queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
                    physicalDevice = dev;
                    computeQueueFamilyIndex = i;
                    break;
                }
            }
            if (physicalDevice != VK_NULL_HANDLE) break;
        }

        if (physicalDevice == VK_NULL_HANDLE) {
            throw std::runtime_error("コンピュートシェーダーを実行可能なGPUが見つかりませんでした。");
        }

        VkPhysicalDeviceProperties deviceProperties;
        vkGetPhysicalDeviceProperties(physicalDevice, &deviceProperties);
        std::cout << "[INFO] 選択されたGPU: " << deviceProperties.deviceName << "\n";
        std::cout << "[INFO] サポートされているAPIバージョン: "
                  << VK_API_VERSION_MAJOR(deviceProperties.apiVersion) << "."
                  << VK_API_VERSION_MINOR(deviceProperties.apiVersion) << "."
                  << VK_API_VERSION_PATCH(deviceProperties.apiVersion) << "\n\n";

        // -------------------------------------------------------------
        // 3. 論理デバイスとキューの作成
        // -------------------------------------------------------------
        float queuePriority = 1.0f;
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = computeQueueFamilyIndex;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;

        VkDeviceCreateInfo deviceCreateInfo{};
        deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceCreateInfo.queueCreateInfoCount = 1;
        deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;

        if (vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &device) != VK_SUCCESS) {
            throw std::runtime_error("論理デバイスの作成に失敗しました。");
        }
        vkGetDeviceQueue(device, computeQueueFamilyIndex, 0, &computeQueue);

        // -------------------------------------------------------------
        // 4. ストレージバッファ (SSBO) の作成
        // -------------------------------------------------------------
        VkDeviceSize bufferSize = sizeof(int32_t) * NUM_ELEMENTS;

        VkBufferCreateInfo bufferCreateInfo{};
        bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferCreateInfo.size = bufferSize;
        bufferCreateInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(device, &bufferCreateInfo, nullptr, &buffer) != VK_SUCCESS) {
            throw std::runtime_error("ストレージバッファの作成に失敗しました。");
        }

        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        // CPUから直接読み込めるよう HOST_VISIBLE かつ HOST_COHERENT を選択
        allocInfo.memoryTypeIndex = findMemoryType(physicalDevice, memRequirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        if (vkAllocateMemory(device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
            throw std::runtime_error("バッファメモリの割り当てに失敗しました。");
        }
        vkBindBufferMemory(device, buffer, bufferMemory, 0);

        // -------------------------------------------------------------
        // 5. ディスクリプタの作成 (SSBOのバインド)
        // -------------------------------------------------------------
        VkDescriptorSetLayoutBinding descriptorLayoutBinding{};
        descriptorLayoutBinding.binding = 0;
        descriptorLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorLayoutBinding.descriptorCount = 1;
        descriptorLayoutBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &descriptorLayoutBinding;

        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
            throw std::runtime_error("ディスクリプタセットレイアウトの作成に失敗しました。");
        }

        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        poolSize.descriptorCount = 1;

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        poolInfo.maxSets = 1;

        if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
            throw std::runtime_error("ディスクリプタプールの作成に失敗しました。");
        }

        VkDescriptorSetAllocateInfo descriptorAllocInfo{};
        descriptorAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        descriptorAllocInfo.descriptorPool = descriptorPool;
        descriptorAllocInfo.descriptorSetCount = 1;
        descriptorAllocInfo.pSetLayouts = &descriptorSetLayout;

        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        if (vkAllocateDescriptorSets(device, &descriptorAllocInfo, &descriptorSet) != VK_SUCCESS) {
            throw std::runtime_error("ディスクリプタセットの割り当てに失敗しました。");
        }

        VkDescriptorBufferInfo bufferDescriptorInfo{};
        bufferDescriptorInfo.buffer = buffer;
        bufferDescriptorInfo.offset = 0;
        bufferDescriptorInfo.range = bufferSize;

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = descriptorSet;
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pBufferInfo = &bufferDescriptorInfo;

        vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);

        // -------------------------------------------------------------
        // 6. パイプラインレイアウトの作成 (Push Constants 登録)
        // -------------------------------------------------------------
        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(PushConstants);

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

        if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
            throw std::runtime_error("パイプラインレイアウトの作成に失敗しました。");
        }

        // -------------------------------------------------------------
        // 7. シェーダーモジュールとコンピュートパイプラインの作成
        // -------------------------------------------------------------
        VkShaderModuleCreateInfo shaderModuleInfo{};
        shaderModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        shaderModuleInfo.codeSize = shaderCode.size();
        shaderModuleInfo.pCode = reinterpret_cast<const uint32_t*>(shaderCode.data());

        if (vkCreateShaderModule(device, &shaderModuleInfo, nullptr, &computeShaderModule) != VK_SUCCESS) {
            throw std::runtime_error("シェーダーモジュールの作成に失敗しました。");
        }

        VkComputePipelineCreateInfo computePipelineInfo{};
        computePipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        computePipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        computePipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        computePipelineInfo.stage.module = computeShaderModule;
        computePipelineInfo.stage.pName = "main";
        computePipelineInfo.layout = pipelineLayout;

        if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &computePipelineInfo, nullptr, &computePipeline) != VK_SUCCESS) {
            throw std::runtime_error("コンピュートパイプラインの作成に失敗しました。");
        }

        // -------------------------------------------------------------
        // 8. コマンドプール & コマンドバッファの作成
        // -------------------------------------------------------------
        VkCommandPoolCreateInfo cmdPoolInfo{};
        cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        cmdPoolInfo.queueFamilyIndex = computeQueueFamilyIndex;

        if (vkCreateCommandPool(device, &cmdPoolInfo, nullptr, &commandPool) != VK_SUCCESS) {
            throw std::runtime_error("コマンドプールの作成に失敗しました。");
        }

        VkCommandBufferAllocateInfo cmdAllocInfo{};
        cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdAllocInfo.commandPool = commandPool;
        cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdAllocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        if (vkAllocateCommandBuffers(device, &cmdAllocInfo, &commandBuffer) != VK_SUCCESS) {
            throw std::runtime_error("コマンドバッファの割り当てに失敗しました。");
        }

        // -------------------------------------------------------------
        // 9. コマンド記録 (Push Constants送信 & ディスパッチ)
        // -------------------------------------------------------------
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(commandBuffer, &beginInfo);

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

        // シェーダーに送る定数データの設定
        PushConstants pushConstants{};
        pushConstants.multiplier = 10;
        pushConstants.offset = 7;

        std::cout << "[INFO] シェーダーへ定数データを送信:\n";
        std::cout << "       - multiplier: " << pushConstants.multiplier << "\n";
        std::cout << "       - offset:     " << pushConstants.offset << "\n\n";

        // 定数データを Push Constants として記録
        vkCmdPushConstants(
            commandBuffer,
            pipelineLayout,
            VK_SHADER_STAGE_COMPUTE_BIT,
            0,
            sizeof(PushConstants),
            &pushConstants
        );

        // 1 ワークグループ (64 スレッド) をディスパッチ
        vkCmdDispatch(commandBuffer, 1, 1, 1);

        vkEndCommandBuffer(commandBuffer);

        // -------------------------------------------------------------
        // 10. コマンドの送信と実行完了待ち
        // -------------------------------------------------------------
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        vkCreateFence(device, &fenceInfo, nullptr, &fence);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        std::cout << "[INFO] GPUでコンピュートシェーダーを実行中...\n";
        vkQueueSubmit(computeQueue, 1, &submitInfo, fence);

        // 完了待機
        vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
        std::cout << "[INFO] 計算が正常に完了しました。\n\n";

        // -------------------------------------------------------------
        // 11. シェーダーからのデータ読み戻し & コンソール表示
        // -------------------------------------------------------------
        void* mappedData = nullptr;
        vkMapMemory(device, bufferMemory, 0, bufferSize, 0, &mappedData);

        int32_t* results = static_cast<int32_t*>(mappedData);

        std::cout << "====================================================\n";
        std::cout << " GPUコンピュートシェーダーからの計算結果 (全 " << NUM_ELEMENTS << " 件)\n";
        std::cout << " 式: results[i] = i * multiplier (" << pushConstants.multiplier << ") + offset (" << pushConstants.offset << ")\n";
        std::cout << "====================================================\n";

        for (uint32_t i = 0; i < NUM_ELEMENTS; i++) {
            std::cout << "Thread [" << std::setw(2) << i << "] : " << std::setw(4) << results[i];
            if ((i + 1) % 4 == 0) {
                std::cout << "\n";
            } else {
                std::cout << "\t";
            }
        }
        std::cout << "====================================================\n\n";

        vkUnmapMemory(device, bufferMemory);

    } catch (const std::exception& e) {
        std::cerr << "[ERROR] エラーが発生しました: " << e.what() << "\n";
    }

    // -------------------------------------------------------------
    // 12. リソースのクリーンアップ
    // -------------------------------------------------------------
    if (device != VK_NULL_HANDLE) {
        if (fence != VK_NULL_HANDLE) vkDestroyFence(device, fence, nullptr);
        if (commandPool != VK_NULL_HANDLE) vkDestroyCommandPool(device, commandPool, nullptr);
        if (computePipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, computePipeline, nullptr);
        if (computeShaderModule != VK_NULL_HANDLE) vkDestroyShaderModule(device, computeShaderModule, nullptr);
        if (pipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        if (descriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, descriptorPool, nullptr);
        if (descriptorSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
        if (bufferMemory != VK_NULL_HANDLE) vkFreeMemory(device, bufferMemory, nullptr);
        if (buffer != VK_NULL_HANDLE) vkDestroyBuffer(device, buffer, nullptr);
        vkDestroyDevice(device, nullptr);
    }
    if (instance != VK_NULL_HANDLE) {
        vkDestroyInstance(instance, nullptr);
    }

    std::cout << "[INFO] すべてのリソースを正常に解放しました。\n";
    return 0;
}
