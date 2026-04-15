import React, { useState, useEffect } from 'react';
import {
  View,
  Text,
  StyleSheet,
  TextInput,
  TouchableOpacity,
  Alert,
  ScrollView,
  ActivityIndicator,
  KeyboardAvoidingView,
  Platform,
} from 'react-native';
import { AppConfig, DEFAULT_CONFIG, getConfig, saveConfig } from '../services/config';
import { listEvents } from '../services/api';

export default function SettingsScreen() {
  const [config, setConfig] = useState<AppConfig>(DEFAULT_CONFIG);
  const [testing, setTesting] = useState(false);
  const [saving, setSaving] = useState(false);

  useEffect(() => {
    getConfig().then(setConfig);
  }, []);

  const updateField = (field: keyof AppConfig, value: string) => {
    setConfig((prev) => ({ ...prev, [field]: value }));
  };

  const handleSave = async () => {
    try {
      setSaving(true);
      await saveConfig(config);
      Alert.alert('Saved', 'Configuration saved successfully.');
    } catch (e: any) {
      Alert.alert('Error', `Failed to save: ${e.message}`);
    } finally {
      setSaving(false);
    }
  };

  const handleTest = async () => {
    try {
      setTesting(true);
      // Save first so the API module uses the current values
      await saveConfig(config);
      const now = Date.now();
      const from = now - 24 * 60 * 60 * 1000;
      const events = await listEvents(from, now, 5);
      Alert.alert(
        'Connection Successful',
        `API responded. Found ${events.length} event(s) in the last 24 hours.`,
      );
    } catch (e: any) {
      Alert.alert('Connection Failed', e.message);
    } finally {
      setTesting(false);
    }
  };

  const handleReset = () => {
    setConfig({ ...DEFAULT_CONFIG });
    Alert.alert('Reset', 'Configuration reset to defaults. Tap Save to persist.');
  };

  return (
    <KeyboardAvoidingView
      style={styles.container}
      behavior={Platform.OS === 'ios' ? 'padding' : undefined}
    >
      <ScrollView contentContainerStyle={styles.content}>
        <Text style={styles.title}>Configuration</Text>
        <Text style={styles.subtitle}>
          These values connect the app to your Lab 7 cloud backend.
        </Text>

        <Text style={styles.label}>API Base URL</Text>
        <TextInput
          style={styles.input}
          value={config.apiUrl}
          onChangeText={(v) => updateField('apiUrl', v)}
          placeholder="https://..."
          placeholderTextColor="#555"
          autoCapitalize="none"
          autoCorrect={false}
        />

        <Text style={styles.label}>Device API Key</Text>
        <TextInput
          style={styles.input}
          value={config.apiKey}
          onChangeText={(v) => updateField('apiKey', v)}
          placeholder="your-api-key"
          placeholderTextColor="#555"
          autoCapitalize="none"
          autoCorrect={false}
        />

        <Text style={styles.label}>Device ID</Text>
        <TextInput
          style={styles.input}
          value={config.deviceId}
          onChangeText={(v) => updateField('deviceId', v)}
          placeholder="esp32p4-birdfeeder"
          placeholderTextColor="#555"
          autoCapitalize="none"
          autoCorrect={false}
        />

        {/* Buttons */}
        <TouchableOpacity style={[styles.btn, styles.btnPrimary]} onPress={handleSave} disabled={saving}>
          {saving ? <ActivityIndicator color="#fff" /> : <Text style={styles.btnText}>💾 Save</Text>}
        </TouchableOpacity>

        <TouchableOpacity
          style={[styles.btn, styles.btnSuccess]}
          onPress={handleTest}
          disabled={testing}
        >
          {testing ? (
            <ActivityIndicator color="#000" />
          ) : (
            <Text style={styles.btnTextDark}>🔗 Test Connection</Text>
          )}
        </TouchableOpacity>

        <TouchableOpacity style={[styles.btn, styles.btnOutline]} onPress={handleReset}>
          <Text style={styles.btnTextOutline}>↺ Reset to Defaults</Text>
        </TouchableOpacity>

        <View style={styles.infoBox}>
          <Text style={styles.infoTitle}>About</Text>
          <Text style={styles.infoText}>
            Bird Feeder Controller — Lab 8{'\n'}
            Sends MQTT commands via REST API → AWS IoT Core → ESP32-P4.{'\n'}
            Live video via LiveKit WebRTC.{'\n'}
            Photos & videos stored in Amazon S3.
          </Text>
        </View>
      </ScrollView>
    </KeyboardAvoidingView>
  );
}

const styles = StyleSheet.create({
  container: { flex: 1, backgroundColor: '#1a1a2e' },
  content: { padding: 20 },
  title: { color: '#e0e0e0', fontSize: 22, fontWeight: 'bold', marginBottom: 4 },
  subtitle: { color: '#888', fontSize: 13, marginBottom: 20 },
  label: { color: '#aaa', fontSize: 13, marginBottom: 4, marginTop: 12 },
  input: {
    backgroundColor: '#0d0d1a',
    color: '#e0e0e0',
    borderRadius: 8,
    padding: 12,
    fontSize: 14,
    borderWidth: 1,
    borderColor: '#333',
  },
  btn: { paddingVertical: 14, borderRadius: 8, alignItems: 'center', marginTop: 14 },
  btnPrimary: { backgroundColor: '#3498db' },
  btnSuccess: { backgroundColor: '#4ecca3' },
  btnOutline: { backgroundColor: 'transparent', borderWidth: 1, borderColor: '#666' },
  btnText: { color: '#fff', fontSize: 15, fontWeight: '600' },
  btnTextDark: { color: '#000', fontSize: 15, fontWeight: '600' },
  btnTextOutline: { color: '#aaa', fontSize: 15, fontWeight: '600' },
  infoBox: {
    backgroundColor: '#16213e',
    borderRadius: 10,
    padding: 16,
    marginTop: 24,
  },
  infoTitle: { color: '#e0e0e0', fontSize: 15, fontWeight: '600', marginBottom: 8 },
  infoText: { color: '#888', fontSize: 12, lineHeight: 20 },
});
