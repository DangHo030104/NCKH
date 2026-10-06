#ifndef LORA_PROTOCOL_H
#define LORA_PROTOCOL_H

void LoRaProtocol_ProcessFrame(char *frame);
void LoRaProtocol_SendTelemetry(void);

#endif
