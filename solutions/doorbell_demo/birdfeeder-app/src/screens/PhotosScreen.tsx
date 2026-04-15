/**
 * PhotosScreen.tsx — Browse and preview photos captured by the ESP32-P4.
 *
 * Lab Feature: "Preview the captured photos on a smartphone"
 *   - Queries DynamoDB (via listEvents API) for snapshot events from the last 7 days
 *   - Displays them in a 3-column grid
 *   - Tapping a photo fetches a pre-signed S3 URL and opens a full-screen preview
 */
import React, { useState, useCallback } from 'react';
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

const SCREEN_WIDTH = Dimensions.get('window').width;
const COLUMN_COUNT = 3;
const THUMB_SIZE = (SCREEN_WIDTH - 48) / COLUMN_COUNT;

export default function PhotosScreen() {
  const [photos, setPhotos] = useState<EventItem[]>([]);
  const [refreshing, setRefreshing] = useState(false);
  const [loading, setLoading] = useState(true);
  const [previewUrl, setPreviewUrl] = useState<string | null>(null);
  const [previewLoading, setPreviewLoading] = useState(false);

  const fetchPhotos = useCallback(async () => {
    try {
      // Fetch events from DynamoDB and filter for snapshots only
      const now = Date.now();
      const from = now - 7 * 24 * 60 * 60 * 1000; // last 7 days
      const events = await listEvents(from, now, 50);
      const snaps = events.filter((e) => e.eventType === 'snapshot');
      setPhotos(snaps);
    } catch (e: any) {
      console.warn('Failed to load photos:', e.message);
    } finally {
      setLoading(false);
      setRefreshing(false);
    }
  }, []);

  useFocusEffect(
    useCallback(() => {
      setLoading(true);
      fetchPhotos();
    }, [fetchPhotos]),
  );

  const onRefresh = () => {
    setRefreshing(true);
    fetchPhotos();
  };

  // Gets a time-limited pre-signed S3 URL for the selected photo
  const openPreview = async (eventId: string) => {
    try {
      setPreviewLoading(true);
      setPreviewUrl(null);
      const urls = await getPlaybackUrls(eventId);
      if (urls.snapshot) {
        setPreviewUrl(urls.snapshot);
      } else {
        console.warn('No snapshot URL for event', eventId);
      }
    } catch (e: any) {
      console.warn('Failed to get playback URL:', e.message);
    } finally {
      setPreviewLoading(false);
    }
  };

  const renderItem = ({ item }: { item: EventItem }) => {
    const time = new Date(item.eventTs).toLocaleString();
    return (
      <TouchableOpacity
        style={styles.thumbContainer}
        onPress={() => openPreview(item.eventId)}
      >
        <View style={styles.thumbPlaceholder}>
          <Text style={styles.thumbIcon}>📷</Text>
        </View>
        <Text style={styles.thumbTime} numberOfLines={1}>
          {time}
        </Text>
      </TouchableOpacity>
    );
  };

  if (loading) {
    return (
      <View style={styles.center}>
        <ActivityIndicator size="large" color="#3498db" />
        <Text style={styles.loadingText}>Loading photos...</Text>
      </View>
    );
  }

  return (
    <View style={styles.container}>
      <FlatList
        data={photos}
        keyExtractor={(item) => item.eventId}
        renderItem={renderItem}
        numColumns={COLUMN_COUNT}
        contentContainerStyle={styles.list}
        refreshControl={<RefreshControl refreshing={refreshing} onRefresh={onRefresh} tintColor="#3498db" />}
        ListEmptyComponent={
          <View style={styles.center}>
            <Text style={styles.emptyIcon}>📷</Text>
            <Text style={styles.emptyText}>No photos yet</Text>
            <Text style={styles.emptySubtext}>
              Go to the Live tab and tap "Capture Photo" to take a snapshot
            </Text>
          </View>
        }
      />

      {/* Full-screen Preview Modal */}
      <Modal visible={previewUrl !== null || previewLoading} transparent animationType="fade">
        <View style={styles.modalBg}>
          <TouchableOpacity style={styles.closeBtn} onPress={() => setPreviewUrl(null)}>
            <Text style={styles.closeBtnText}>✕</Text>
          </TouchableOpacity>
          {previewLoading ? (
            <ActivityIndicator size="large" color="#fff" />
          ) : previewUrl ? (
            <Image source={{ uri: previewUrl }} style={styles.previewImage} resizeMode="contain" />
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
  thumbContainer: { width: THUMB_SIZE, margin: 4, alignItems: 'center' },
  thumbPlaceholder: {
    width: THUMB_SIZE - 8,
    height: THUMB_SIZE - 8,
    backgroundColor: '#16213e',
    borderRadius: 8,
    justifyContent: 'center',
    alignItems: 'center',
  },
  thumbIcon: { fontSize: 32 },
  thumbTime: { color: '#888', fontSize: 10, marginTop: 4, textAlign: 'center' },
  emptyIcon: { fontSize: 48, marginBottom: 12 },
  emptyText: { color: '#e0e0e0', fontSize: 18, fontWeight: '600', marginBottom: 8 },
  emptySubtext: { color: '#888', fontSize: 13, textAlign: 'center', lineHeight: 20 },
  modalBg: {
    flex: 1,
    backgroundColor: 'rgba(0,0,0,0.92)',
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
  previewImage: { width: SCREEN_WIDTH - 32, height: (SCREEN_WIDTH - 32) * 0.75, borderRadius: 10 },
});
