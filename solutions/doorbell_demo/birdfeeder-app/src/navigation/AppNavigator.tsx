import React from 'react';
import { createBottomTabNavigator } from '@react-navigation/bottom-tabs';
import { Ionicons } from '@expo/vector-icons';

import LiveViewScreen from '../screens/LiveViewScreen';
import PhotosScreen from '../screens/PhotosScreen';
import VideosScreen from '../screens/VideosScreen';
import SettingsScreen from '../screens/SettingsScreen';

const Tab = createBottomTabNavigator();

export default function AppNavigator() {
  return (
    <Tab.Navigator
      screenOptions={{
        headerStyle: { backgroundColor: '#0f3460' },
        headerTintColor: '#e0e0e0',
        tabBarStyle: { backgroundColor: '#16213e', borderTopColor: '#0f3460' },
        tabBarActiveTintColor: '#4ecca3',
        tabBarInactiveTintColor: '#888',
      }}
    >
      <Tab.Screen
        name="Live"
        component={LiveViewScreen}
        options={{
          title: 'Live View',
          tabBarIcon: ({ color, size }) => (
            <Ionicons name="videocam" size={size} color={color} />
          ),
        }}
      />
      <Tab.Screen
        name="Photos"
        component={PhotosScreen}
        options={{
          title: 'Photos',
          tabBarIcon: ({ color, size }) => (
            <Ionicons name="images" size={size} color={color} />
          ),
        }}
      />
      <Tab.Screen
        name="Videos"
        component={VideosScreen}
        options={{
          title: 'Videos',
          tabBarIcon: ({ color, size }) => (
            <Ionicons name="film" size={size} color={color} />
          ),
        }}
      />
      <Tab.Screen
        name="Settings"
        component={SettingsScreen}
        options={{
          title: 'Settings',
          tabBarIcon: ({ color, size }) => (
            <Ionicons name="settings" size={size} color={color} />
          ),
        }}
      />
    </Tab.Navigator>
  );
}
