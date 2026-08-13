/*
 * mqtt_handler.h
 */

#ifndef INC_MQTT_HANDLER_H_
#define INC_MQTT_HANDLER_H_

#include "MQTTClient.h"

void MQTT_MessageArrived(MessageData* md);

/* MQTTYield 콜백 밖에서 stat 발행 (버퍼 재진입 deadlock 방지) */
void MqttDeferred_Flush(void);

void MqttPending_Process(void);

/* 하위 호환 별칭 */
void MqttAd_ProcessPending(void);

#endif /* INC_MQTT_HANDLER_H_ */
