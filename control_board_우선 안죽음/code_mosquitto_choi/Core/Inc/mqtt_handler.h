/*
 * mqtt_handler.h
 *
 *  Created on: Mar 3, 2026
 *      Author: cshss
 */

#ifndef INC_MQTT_HANDLER_H_
#define INC_MQTT_HANDLER_H_

#include "MQTTClient.h"


/* MQTT 메시지 수신 콜백 함수 선언 */
void MQTT_MessageArrived(MessageData* md);

#endif /* INC_MQTT_HANDLER_H_ */
