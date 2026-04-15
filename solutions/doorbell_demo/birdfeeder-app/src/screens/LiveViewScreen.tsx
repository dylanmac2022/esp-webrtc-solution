import React, { useState } from 'react';
import {
  View,
  Text,
  StyleSheet,
  TouchableOpacity,
  Alert,
  ActivityIndicator,
  ScrollView,
} from 'react-native';
import { Track } from 'livekit-client';
import {
  LiveKitRoom,
  useTracks,
  VideoTrack,
} from '@livekit/react-native';
import { sendCommand, getViewerToken } from '../services/api';

type ConnectionStatus = 'disconnected' | 'connecting' | 'live';

/** Inner component that renders remote video tracks inside LiveKitRoom context */
function RemoteVideo() {
  const videoTracks = useTracks([Track.Source.Camera], {
    onlySubscribed: true,
  });
  // Subscribe to audio — playback is handled natively by LiveKit after AudioSession.startAudioSession()
  useTracks([Track.Source.Microphone], { onlySubscribed: true });

  return (
    <View style={styles.videoContainer}>
      {videoTracks.length > 0 ? (
        <>
          <VideoTrack
            trackRef={videoTracks[0]}
            style={{ width: '100%', height: '100%' } as any}
            objectFit="contain"
          />
          <View style={styles.liveBadge}>
            <Text style={styles.liveBadgeText}>🔴 LIVE</Text>
          </View>
        </>
      ) : (
        <Text style={styles.videoPlaceholder}>Waiting for camera stream...</Text>
      )}
    </View>
  );
}

export default function LiveViewScreen() {
  const [status, setStatus] = useState<ConnectionStatus>('disconnected');
  const [recording, setRecording] = useState(false);
  const [sending, setSending] = useState<string | null>(null);
  const [logs, setLogs] = useState<string[]>([]);
  const [token, setToken] = useState<string | null>(null);
  const [wsUrl, setWsUrl] = useState<string | null>(null);

  const addLog = (msg: string) => {
    const ts = new Date().toLocaleTimeString();
    setLogs((prev) => [`[${ts}] ${msg}`, ...prev].slice(0, 50));
  };

  const handleConnect = async () => {
    if (status === 'live') {
      setStatus('disconnected');
      setToken(null);
      setWsUrl(null);
      addLog('Disconnected from live view');
      return;
    }

    try {
      setStatus('connecting');
      addLog('Requesting LiveKit viewer token...');
      const data = await getViewerToken();
      setToken(data.token);
      setWsUrl(data.wsUrl);
      setStatus('live');
      addLog(`Connected to room: ${data.roomName}`);
    } catch (e: any) {
      setStatus('disconnected');
      addLog(`Connection failed: ${e.message}`);
      Alert.alert('Connection Error', e.message);
    }
  };

  const handleCommand = async (command: string) => {
    try {
      setSending(command);
      addLog(`Sending command: ${command}`);
      await sendCommand(command);
      addLog(`Command sent: ${command}`);
    } catch (e: any) {
      addLog(`Command failed: ${e.message}`);
      Alert.alert('Command Error', e.message);
    } finally {
      setSending(null);
    }
  };

  const handleSnapshot = () => handleCommand('capture_snapshot');

  const handleToggleRecording = async () => {
    if (!recording) {
      await handleCommand('record_start');
      setRecording(true);
    } else {
      await handleCommand('record_stop');
      setRecording(false);
    }
  };

  const statusColor =
    status === 'live' ? '#4ecca3' : status === 'connecting' ? '#f39c12' : '#e74c3c';

  return (
    <ScrollView style={styles.container} contentContainerStyle={styles.content}>
      {/* Status Bar */}
      <View style={styles.statusRow}>
        <View style={[styles.dot, { backgroundColor: statusColor }]} />
        <Text style={styles.statusText}>
          {status === 'live' ? 'Live' : status === 'connecting' ? 'Connecting...' : 'Disconnected'}
        </Text>
      </View>

      {/* Video Area */}
      {status === 'live' && token && wsUrl ? (
        <LiveKitRoom
          serverUrl={wsUrl}
          token={token}
          connect={true}
          audio={true}
          video={false}
        >
          <RemoteVideo />
        </LiveKitRoom>
      ) : (
        <View style={styles.videoContainer}>
          <Text style={styles.videoPlaceholder}>
            {status === 'connecting' ? 'Connecting...' : 'Tap "Connect" to start live view'}
          </Text>
        </View>
      )}

      {/* Connect / Disconnect Button */}
      <TouchableOpacity
        style={[styles.btn, status === 'live' ? styles.btnDanger : styles.btnPrimary]}
        onPress={handleConnect}
        disabled={status === 'connecting'}
      >
        {status === 'connecting' ? (
          <ActivityIndicator color="#fff" />
        ) : (
          <Text style={styles.btnText}>{status === 'live' ? 'Disconnect' : 'Connect'}</Text>
        )}
      </TouchableOpacity>

      {/* Command Buttons */}
      <View style={styles.controlRow}>
        <TouchableOpacity
          style={[styles.btn, styles.btnSuccess, { flex: 1, marginRight: 8 }]}
          onPress={handleSnapshot}
          disabled={sending !== null}
        >
          {sending === 'capture_snapshot' ? (
            <ActivityIndicator color="#000" />
          ) : (
            <Text style={styles.btnTextDark}>📷 Capture Photo</Text>
          )}
        </TouchableOpacity>

        <TouchableOpacity
          style={[styles.btn, recording ? styles.btnDanger : styles.btnRecord, { flex: 1 }]}
          onPress={handleToggleRecording}
          disabled={sending !== null}
        >
          {sending === 'record_start' || sending === 'record_stop' ? (
            <ActivityIndicator color="#fff" />
          ) : (
            <Text style={styles.btnText}>
              {recording ? '⏹ Stop Recording' : '⏺ Start Recording'}
            </Text>
          )}
        </TouchableOpacity>
      </View>

      {/* Log */}
      <Text style={styles.logTitle}>Activity Log</Text>
      <View style={styles.logContainer}>
        {logs.length === 0 ? (
          <Text style={styles.logEmpty}>No activity yet</Text>
        ) : (
          logs.map((entry, i) => (
            <Text key={i} style={styles.logEntry}>
              {entry}
            </Text>
          ))
        )}
      </View>
    </ScrollView>
  );
}

const styles = StyleSheet.create({
  container: { flex: 1, backgroundColor: '#1a1a2e' },
  content: { padding: 16 },
  statusRow: { flexDirection: 'row', alignItems: 'center', marginBottom: 12 },
  dot: { width: 12, height: 12, borderRadius: 6, marginRight: 8 },
  statusText: { color: '#e0e0e0', fontSize: 16, fontWeight: '600' },
  videoContainer: {
    backgroundColor: '#000',
    borderRadius: 10,
    aspectRatio: 4 / 3,
    justifyContent: 'center',
    alignItems: 'center',
    marginBottom: 16,
    overflow: 'hidden',
  },
  videoPlaceholder: { color: '#666', textAlign: 'center', fontSize: 15 },
  liveBadge: {
    position: 'absolute',
    top: 10,
    left: 10,
    backgroundColor: 'rgba(0,0,0,0.6)',
    paddingHorizontal: 8,
    paddingVertical: 4,
    borderRadius: 6,
  },
  liveBadgeText: { color: '#e74c3c', fontSize: 13, fontWeight: 'bold' },
  controlRow: { flexDirection: 'row', marginBottom: 16 },
  btn: { paddingVertical: 14, borderRadius: 8, alignItems: 'center', marginBottom: 10 },
  btnPrimary: { backgroundColor: '#3498db' },
  btnSuccess: { backgroundColor: '#4ecca3' },
  btnDanger: { backgroundColor: '#e74c3c' },
  btnRecord: { backgroundColor: '#c0392b' },
  btnText: { color: '#fff', fontSize: 15, fontWeight: '600' },
  btnTextDark: { color: '#000', fontSize: 15, fontWeight: '600' },
  logTitle: { color: '#888', fontSize: 13, marginBottom: 6, fontWeight: '600' },
  logContainer: {
    backgroundColor: '#0d0d1a',
    borderRadius: 8,
    padding: 10,
    maxHeight: 200,
  },
  logEmpty: { color: '#555', fontStyle: 'italic', textAlign: 'center' },
  logEntry: { color: '#bbb', fontSize: 11, fontFamily: 'monospace', marginBottom: 2 },
});
