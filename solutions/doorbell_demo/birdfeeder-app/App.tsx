import React, { useEffect, useState } from 'react';
import { StatusBar } from 'expo-status-bar';
import { NavigationContainer } from '@react-navigation/native';
import { View, Text, ActivityIndicator } from 'react-native';
import AppNavigator from './src/navigation/AppNavigator';

export default function App() {
  const [ready, setReady] = useState(false);

  useEffect(() => {
    let mounted = true;
    (async () => {
      try {
        const { registerGlobals } = await import('@livekit/react-native');
        registerGlobals();
      } catch (e) {
        console.warn('LiveKit registerGlobals failed (non-fatal):', e);
      }
      if (mounted) setReady(true);
    })();
    return () => { mounted = false; };
  }, []);

  if (!ready) {
    return (
      <View style={{ flex: 1, justifyContent: 'center', alignItems: 'center', backgroundColor: '#1a1a2e' }}>
        <ActivityIndicator size="large" color="#4cc9f0" />
        <Text style={{ color: '#fff', marginTop: 12 }}>Initializing...</Text>
      </View>
    );
  }

  return (
    <NavigationContainer>
      <StatusBar style="light" />
      <AppNavigator />
    </NavigationContainer>
  );
}
