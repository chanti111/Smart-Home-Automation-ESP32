import { Buffer } from 'buffer';
import React, { useState } from 'react';
import {
  Alert,
  PermissionsAndroid,
  Platform,
  ScrollView,
  StyleSheet,
  Text,
  View,
} from 'react-native';
import { BleManager, Device } from 'react-native-ble-plx';
import { Button, Card } from 'react-native-paper';

const manager = new BleManager();

const DEVICE_NAME = 'ESP32_HOME_BLE';

const SERVICE_UUID = '000000ff-0000-1000-8000-00805f9b34fb';
const CHARACTERISTIC_UUID = '0000ff01-0000-1000-8000-00805f9b34fb';

export default function BleHomeDeviceControl() {
  const [device, setDevice] = useState<Device | null>(null);
  const [status, setStatus] = useState('Disconnected');
  const [scanning, setScanning] = useState(false);

  const requestPermissions = async () => {
    if (Platform.OS === 'android') {
      if (Platform.Version >= 31) {
        await PermissionsAndroid.requestMultiple([
          PermissionsAndroid.PERMISSIONS.BLUETOOTH_SCAN,
          PermissionsAndroid.PERMISSIONS.BLUETOOTH_CONNECT,
          PermissionsAndroid.PERMISSIONS.ACCESS_FINE_LOCATION,
        ]);
      } else {
        await PermissionsAndroid.request(
          PermissionsAndroid.PERMISSIONS.ACCESS_FINE_LOCATION
        );
      }
    }
  };

  const scanAndConnect = async () => {
    await requestPermissions();

    setStatus('Scanning...');
    setScanning(true);

    manager.stopDeviceScan();

    manager.startDeviceScan(null, null, async (error, scannedDevice) => {
      if (error) {
        console.log('Scan Error:', error);
        setStatus('Scan Error: ' + error.message);
        setScanning(false);
        return;
      }

      const name = scannedDevice?.name || scannedDevice?.localName;

      console.log('Found Device:', name, scannedDevice?.id);

      if (name === DEVICE_NAME && scannedDevice) {
        manager.stopDeviceScan();
        setScanning(false);

        try {
          setStatus('Connecting...');

          const connectedDevice = await scannedDevice.connect({
            timeout: 10000,
            autoConnect: false,
          });

          setStatus('Discovering Services...');

          const readyDevice =
            await connectedDevice.discoverAllServicesAndCharacteristics();

          setDevice(readyDevice);
          setStatus('Connected to ESP32_HOME_BLE');

          Alert.alert('Success', 'ESP32 BLE Connected Successfully');
        } catch (e: any) {
          console.log('Connection Error:', e);
          setStatus('Connection Failed: ' + e.message);
        }
      }
    });
  };

  const sendCommand = async (command: string) => {
    if (!device) {
      setStatus('Device not connected');
      Alert.alert('Error', 'Please connect ESP32 first');
      return;
    }

    try {
      const base64Command = Buffer.from(command).toString('base64');

      await device.writeCharacteristicWithoutResponseForService(
        SERVICE_UUID,
        CHARACTERISTIC_UUID,
        base64Command
      );

      setStatus(`Sent: ${command}`);
    } catch (e: any) {
      console.log('Command Send Error:', e);
      setStatus('Command Send Failed: ' + e.message);
    }
  };

  const disconnect = async () => {
    try {
      manager.stopDeviceScan();
      setScanning(false);

      if (device) {
        await device.cancelConnection();
        setDevice(null);
      }

      setStatus('Disconnected');
    } catch (e: any) {
      setStatus('Disconnect Failed: ' + e.message);
    }
  };

  return (
    <ScrollView contentContainerStyle={styles.container}>
      <Text style={styles.title}>🏠 BLE Home Device Control</Text>

      <Text style={styles.subtitle}>
        ESP32 ESP-IDF Embedded C • React Native BLE
      </Text>

      <Card style={styles.statusCard}>
        <Card.Content>
          <Text style={styles.statusText}>Status: {status}</Text>

          <Button
            mode="contained"
            onPress={scanAndConnect}
            disabled={scanning}
            style={styles.button}
          >
            {scanning ? 'Scanning...' : 'Scan & Connect'}
          </Button>

          <Button mode="outlined" onPress={disconnect} style={styles.button}>
            Disconnect
          </Button>
        </Card.Content>
      </Card>

      <LedControl
        title="LED 1"
        onPressOn={() => sendCommand('LED1_ON')}
        onPressOff={() => sendCommand('LED1_OFF')}
      />

      <LedControl
        title="LED 2"
        onPressOn={() => sendCommand('LED2_ON')}
        onPressOff={() => sendCommand('LED2_OFF')}
      />

      <LedControl
        title="LED 3"
        onPressOn={() => sendCommand('LED3_ON')}
        onPressOff={() => sendCommand('LED3_OFF')}
      />

      <LedControl
        title="LED 4"
        onPressOn={() => sendCommand('LED4_ON')}
        onPressOff={() => sendCommand('LED4_OFF')}
      />

      <LedControl
        title="LED 5"
        onPressOn={() => sendCommand('LED5_ON')}
        onPressOff={() => sendCommand('LED5_OFF')}
      />

      <Card style={styles.card}>
        <Card.Content>
          <Text style={styles.cardTitle}>All Devices</Text>

          <View style={styles.row}>
            <Button
              mode="contained"
              onPress={() => sendCommand('ALL_ON')}
              style={styles.controlButton}
            >
              ALL ON
            </Button>

            <Button
              mode="contained-tonal"
              onPress={() => sendCommand('ALL_OFF')}
              style={styles.controlButton}
            >
              ALL OFF
            </Button>
          </View>
        </Card.Content>
      </Card>
    </ScrollView>
  );
}

function LedControl({
  title,
  onPressOn,
  onPressOff,
}: {
  title: string;
  onPressOn: () => void;
  onPressOff: () => void;
}) {
  return (
    <Card style={styles.card}>
      <Card.Content>
        <Text style={styles.cardTitle}>💡 {title}</Text>

        <View style={styles.row}>
          <Button mode="contained" onPress={onPressOn} style={styles.controlButton}>
            ON
          </Button>

          <Button
            mode="contained-tonal"
            onPress={onPressOff}
            style={styles.controlButton}
          >
            OFF
          </Button>
        </View>
      </Card.Content>
    </Card>
  );
}

const styles = StyleSheet.create({
  container: {
    flexGrow: 1,
    backgroundColor: '#0F172A',
    padding: 20,
  },

  title: {
    fontSize: 28,
    fontWeight: 'bold',
    color: '#FFFFFF',
    textAlign: 'center',
    marginTop: 20,
  },

  subtitle: {
    fontSize: 15,
    color: '#CBD5E1',
    textAlign: 'center',
    marginBottom: 20,
  },

  statusCard: {
    backgroundColor: '#1E293B',
    borderRadius: 18,
    marginBottom: 15,
  },

  statusText: {
    color: '#FFFFFF',
    fontSize: 16,
    marginBottom: 10,
  },

  card: {
    backgroundColor: '#1E293B',
    borderRadius: 18,
    marginBottom: 12,
  },

  cardTitle: {
    color: '#FFFFFF',
    fontSize: 20,
    fontWeight: 'bold',
    marginBottom: 12,
  },

  row: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    gap: 10,
  },

  button: {
    marginTop: 10,
  },

  controlButton: {
    flex: 1,
  },
});
