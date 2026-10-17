#pragma once
#include <cstddef>
#include <cstdint>
// Keep upload work bounded without scheduling a GPU fence for every asset.
// An empty completion batch is significant: it fences preceding submissions.
constexpr bool Web_SubmitPreparationResources(std::size_t commands, std::uint64_t bytes, bool completion, bool flush) {
    return completion || (commands && (flush || commands >= 512 || bytes >= (4u << 20)));
}
constexpr bool Web_ShaderWarmPolicy(int mode, bool webgpu) {
    return mode == 2 || (mode == 1 && webgpu);
}
constexpr bool Web_ResidentStaticImagePolicy(bool full, bool webgpu) {
    return full && webgpu;
}
constexpr bool Web_PipelineNostallPolicy(bool disabled) {
    return !disabled;
}
constexpr unsigned Web_RuntimePipelineJobs(unsigned preparationJobs) {
    return preparationJobs == 4 || preparationJobs == 16 ? preparationJobs : 8;
}
constexpr double Web_SettleMinMs() { return 250; }
constexpr double Web_SettleTimeoutMs() { return 45000; }
constexpr bool Web_SettleFinished(unsigned pipelineJobs, double elapsedMs, bool cancelled = false) {
    return cancelled || elapsedMs >= Web_SettleTimeoutMs() || (elapsedMs >= Web_SettleMinMs() && pipelineJobs == 0);
}
// Speculative warmup recipes compile during the gate. Only capture exports and
// later required gameplay draws persist into the learned 64 MB cache.
constexpr bool Web_PersistWarmRecipe(bool capture, bool learned) {
    return capture || learned;
}
// Temporary nostall retest: gameplay map loads skip the blocking graphics gate.
// Catalog capture still walks maps when preparation is full. Frontend warmup
// is a separate call and does not use this predicate.
constexpr bool Web_SkipMapLoadPreparation(int mode, bool capture) {
    return !(mode == 2 && capture);
}
