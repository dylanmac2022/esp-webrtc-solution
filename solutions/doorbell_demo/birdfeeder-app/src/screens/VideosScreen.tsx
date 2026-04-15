/**
 * VideosScreen.tsx — Browse and play back MJPEG videos recorded by the ESP32-P4.
 *
 * Lab Feature: "Play back the recorded videos on a smartphone"
 *   - Queries DynamoDB for recording events, lists them with timestamps
 *   - Downloads the MJPEG AVI file from S3 (pre-signed URL)
 *   - Parses the AVI to extract individual JPEG frames (mjpegParser.ts)
 *   - Plays frames back at ~2 FPS using setInterval + base64 Image rendering
 */
import React, { useState, useCallback, useRef, useEffect } from 'react';
import {
  View,
  Text,
  StyleSheet,
  FlatList,
  TouchableOpacity,
  Image,
  Modal,
  ActivityIndicator,
  RefreshControl,
  Dimensions,
} from 'react-native';
import { useFocusEffect } from '@react-navigation/native';
import { listEvents, getPlaybackUrls, EventItem } from '../services/api';
import { parseMjpegAvi } from '../services/mjpegParser';

const SCREEN_WIDTH = Dimensions.get('window').width;

export default function VideosScreen() {
  const [videos, setVideos] = useState<EventItem[]>([]);
  const [refreshing, setRefreshing] = useState(false);
  const [loading, setLoading] = useState(true);

  // Playback state
  const [playbackFrames, setPlaybackFrames] = useState<string[]>([]);
  const [currentFrame, setCurrentFrame] = useState(0);
  const [playing, setPlaying] = useState(false);
  const [downloading, setDownloading] = useState(false);
  const timerRef = useRef<ReturnType<typeof setInterval> | null>(null);

  const fetchVideos = useCallback(async () => {
    try {
      const now = Date.now();
      const from = now - 7 * 24 * 60 * 60 * 1000;
      const events = await listEvents(from, now, 50);
      const recs = events.filter((e) => e.eventType === 'recording');
      setVideos(recs);
    } catch (e: any) {
      console.warn('Failed to load videos:', e.message);
    } finally {
      setLoading(false);
      setRefreshing(false);
    }
  }, []);

  useFocusEffect(
    useCallback(() => {
      setLoading(true);
      fetchVideos();
    }, [fetchVideos]),
  );

  const onRefresh = () => {
    setRefreshing(true);
    fetchVideos();
  };

  // Clean up timer on unmount
  useEffect(() => {
    return () => {
      if (timerRef.current) clearInterval(timerRef.current);
    };
  }, []);

  // Playback timer
  useEffect(() => {
    if (playing && playbackFrames.length > 0) {
      // Frame-by-frame playback: cycles through extracted JPEG frames at 2 FPS
      timerRef.current = setInterval(() => {
        setCurrentFrame((prev) => {
          if (prev >= playbackFrames.length - 1) {
            return 0; // loop
          }
          return prev + 1;
        });
      }, 500); // 500ms = 2 FPS
    } else {
      if (timerRef.current) {
        clearInterval(timerRef.current);
        timerRef.current = null;
      }
    }
    return () => {
      if (timerRef.current) {
        clearInterval(timerRef.current);
        timerRef.current = null;
      }
    };
  }, [playing, playbackFrames]);

  // Downloads AVI from S3, parses JPEG frames, and prepares for playback
  const openPlayback = async (eventId: string) => {
    try {
      setDownloading(true);
      setPlaybackFrames([]);
      setCurrentFrame(0);
      setPlaying(false);

      const urls = await getPlaybackUrls(eventId);
      if (!urls.video) {
        console.warn('No video URL for event', eventId);
        setDownloading(false);
        return;
      }

      // Fetch the AVI binary
      const resp = await fetch(urls.video);
      const buf = await resp.arrayBuffer();
      const frames = parseMjpegAvi(buf);

      if (frames.length === 0) {
        console.warn('No JPEG frames found in AVI');
        setDownloading(false);
        return;
      }

      setPlaybackFrames(frames);
      setCurrentFrame(0);
      setPlaying(true);
    } catch (e: any) {
      console.warn('Playback error:', e.message);
    } finally {
      setDownloading(false);
    }
  };

  const closePlayback = () => {
    setPlaying(false);
    setPlaybackFrames([]);
    setCurrentFrame(0);
  };

  const togglePlayPause = () => {
    setPlaying((p) => !p);
  };

  const renderItem = ({ item }: { item: EventItem }) => {
    const time = new Date(item.eventTs).toLocaleString();
    return (
      <TouchableOpacity style={styles.videoItem} onPress={() => openPlayback(item.eventId)}>
        <View style={styles.videoIcon}>
          <Text style={{ fontSize: 28 }}>🎬</Text>
        </View>
        <View style={styles.videoInfo}>
          <Text style={styles.videoType}>Recording</Text>
          <Text style={styles.videoTime}>{time}</Text>
        </View>
        <TouchableOpacity
          style={styles.playBtn}
          onPress={() => openPlayback(item.eventId)}
        >
          <Text style={styles.playBtnText}>▶ Play</Text>
        </TouchableOpacity>
      </TouchableOpacity>
    );
  };

  if (loading) {
    return (
      <View style={styles.center}>
        <ActivityIndicator size="large" color="#3498db" />
        <Text style={styles.loadingText}>Loading videos...</Text>
      </View>
    );
  }

  return (
    <View style={styles.container}>
      <FlatList
        data={videos}
        keyExtractor={(item) => item.eventId}
        renderItem={renderItem}
        contentContainerStyle={styles.list}
        refreshControl={<RefreshControl refreshing={refreshing} onRefresh={onRefresh} tintColor="#3498db" />}
        ListEmptyComponent={
          <View style={styles.center}>
            <Text style={styles.emptyIcon}>🎬</Text>
            <Text style={styles.emptyText}>No videos yet</Text>
            <Text style={styles.emptySubtext}>
              Go to the Live tab, start a recording, wait a few seconds, then stop it
            </Text>
          </View>
        }
      />

      {/* Playback Modal */}
      <Modal visible={playbackFrames.length > 0 || downloading} transparent animationType="fade">
        <View style={styles.modalBg}>
          <TouchableOpacity style={styles.closeBtn} onPress={closePlayback}>
            <Text style={styles.closeBtnText}>✕</Text>
          </TouchableOpacity>

          {downloading ? (
            <View style={styles.downloadingContainer}>
              <ActivityIndicator size="large" color="#fff" />
              <Text style={styles.downloadingText}>Downloading recording...</Text>
            </View>
          ) : playbackFrames.length > 0 ? (
            <View style={styles.playerContainer}>
              <Image
                source={{ uri: `data:image/jpeg;base64,${playbackFrames[currentFrame]}` }}
                style={styles.playerImage}
                resizeMode="contain"
              />
              <View style={styles.playerControls}>
                <TouchableOpacity style={styles.ppBtn} onPress={togglePlayPause}>
                  <Text style={styles.ppBtnText}>{playing ? '⏸' : '▶️'}</Text>
                </TouchableOpacity>
                <Text style={styles.frameInfo}>
                  Frame {currentFrame + 1} / {playbackFrames.length}
                </Text>
              </View>
            </View>
          ) : null}
        </View>
      </Modal>
    </View>
  );
}

const styles = StyleSheet.create({
  container: { flex: 1, backgroundColor: '#1a1a2e' },
  list: { padding: 12 },
  center: { flex: 1, justifyContent: 'center', alignItems: 'center', backgroundColor: '#1a1a2e', padding: 32 },
  loadingText: { color: '#888', marginTop: 12 },
  videoItem: {
    flexDirection: 'row',
    alignItems: 'center',
    backgroundColor: '#16213e',
    borderRadius: 10,
    padding: 14,
    marginBottom: 10,
  },
  videoIcon: { marginRight: 14 },
  videoInfo: { flex: 1 },
  videoType: { color: '#e0e0e0', fontSize: 15, fontWeight: '600' },
  videoTime: { color: '#888', fontSize: 12, marginTop: 2 },
  playBtn: {
    backgroundColor: '#4ecca3',
    paddingHorizontal: 14,
    paddingVertical: 8,
    borderRadius: 6,
  },
  playBtnText: { color: '#000', fontWeight: '600', fontSize: 13 },
  emptyIcon: { fontSize: 48, marginBottom: 12 },
  emptyText: { color: '#e0e0e0', fontSize: 18, fontWeight: '600', marginBottom: 8 },
  emptySubtext: { color: '#888', fontSize: 13, textAlign: 'center', lineHeight: 20 },
  modalBg: {
    flex: 1,
    backgroundColor: 'rgba(0,0,0,0.95)',
    justifyContent: 'center',
    alignItems: 'center',
  },
  closeBtn: {
    position: 'absolute',
    top: 50,
    right: 20,
    backgroundColor: '#e74c3c',
    width: 36,
    height: 36,
    borderRadius: 18,
    justifyContent: 'center',
    alignItems: 'center',
    zIndex: 10,
  },
  closeBtnText: { color: '#fff', fontSize: 18, fontWeight: 'bold' },
  downloadingContainer: { alignItems: 'center' },
  downloadingText: { color: '#ccc', marginTop: 12, fontSize: 14 },
  playerContainer: { alignItems: 'center' },
  playerImage: {
    width: SCREEN_WIDTH - 32,
    height: (SCREEN_WIDTH - 32) * 0.75,
    borderRadius: 10,
  },
  playerControls: {
    flexDirection: 'row',
    alignItems: 'center',
    marginTop: 16,
    gap: 16,
  },
  ppBtn: {
    backgroundColor: '#333',
    width: 48,
    height: 48,
    borderRadius: 24,
    justifyContent: 'center',
    alignItems: 'center',
  },
  ppBtnText: { fontSize: 22 },
  frameInfo: { color: '#ccc', fontSize: 14 },
});
