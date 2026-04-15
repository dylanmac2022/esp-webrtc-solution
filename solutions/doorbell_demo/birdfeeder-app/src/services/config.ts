/**
 * config.ts — Stores the cloud backend configuration.
 *
 * Default values point to the Lab 7 AWS deployment:
 *   - apiUrl:    API Gateway endpoint (routes REST calls to Lambda)
 *   - apiKey:    Shared key for authenticating API requests
 *   - deviceId:  ESP32-P4 device identifier used in MQTT topics
 *
 * Settings are persisted to AsyncStorage so they survive app restarts.
 */
import AsyncStorage from '@react-native-async-storage/async-storage';

export interface AppConfig {
  apiUrl: string;
  apiKey: string;
  deviceId: string;
}

const STORAGE_KEY = 'birdfeeder_config';

export const DEFAULT_CONFIG: AppConfig = {
  apiUrl: 'https://nex0zvlly4.execute-api.us-east-2.amazonaws.com',
  apiKey: 'doorbell-lab7-key-2026-04-02-9f7c',
  deviceId: 'esp32p4-birdfeeder',
};

export async function getConfig(): Promise<AppConfig> {
  try {
    const raw = await AsyncStorage.getItem(STORAGE_KEY);
    if (raw) {
      return { ...DEFAULT_CONFIG, ...JSON.parse(raw) };
    }
  } catch {}
  return { ...DEFAULT_CONFIG };
}

export async function saveConfig(config: AppConfig): Promise<void> {
  await AsyncStorage.setItem(STORAGE_KEY, JSON.stringify(config));
}
