#include "c3/device_app.hpp"

namespace c3::device
{
ApplyResult apply_ui(Runtime& runtime, UiRequest request, std::unique_ptr<PetPack> pack)
{
    if (!runtime.ui_sender || xSemaphoreTake(runtime.ui_sender, pdMS_TO_TICKS(100)) != pdTRUE)
        return ApplyResult::busy;
    // USB and HTTP share one reply mailbox. A late reply after a timeout can only
    // be consumed by a later request, which checks the ID before accepting it.
    UiReply reply;
    while (xQueueReceive(runtime.ui_replies, &reply, 0) == pdTRUE) {}
    request.id = ++runtime.request_id;
    request.pack = pack.get();
    ApplyResult result = ApplyResult::busy;
    if (xQueueSend(runtime.ui_requests, &request, 0) == pdTRUE)
    {
        pack.release();
        result = ApplyResult::timeout;
        auto left = pdMS_TO_TICKS(1500);
        TimeOut_t started;
        vTaskSetTimeOutState(&started);
        while (xQueueReceive(runtime.ui_replies, &reply, left) == pdTRUE)
        {
            if (reply.id == request.id)
            {
                result = reply.ok ? ApplyResult::applied : ApplyResult::unavailable;
                break;
            }
            if (xTaskCheckForTimeOut(&started, &left) == pdTRUE) break;
        }
    }
    xSemaphoreGive(runtime.ui_sender);
    return result;
}
}  // namespace c3::device
