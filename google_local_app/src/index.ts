import { App, DataFlow, DeviceManager, IntentFlow } from '@google/local-home-sdk';

const APP_ID = 'sea-local-home';
const DEFAULT_DEVICE_ID = 'sea.sea';
const DEFAULT_HOSTNAME = 'sea.local';

interface CustomData {
  deviceId: string;
  hostname?: string;
  port?: number;
}

class SeaLocalHome {
  private readonly deviceManager: DeviceManager;

  constructor(private readonly app: App) {
    this.deviceManager = app.getDeviceManager();
    app.onIdentify(this.onIdentify.bind(this));
    app.onExecute(this.onExecute.bind(this));
    app.onQuery(this.onQuery.bind(this));
    app.listen();
  }

  private portFromData(data?: CustomData): number {
    return data?.port || 80;
  }

  private hostnameFromData(data?: CustomData): string {
    return data?.hostname || DEFAULT_HOSTNAME;
  }

  private async sendHttpRequest<T>(options: {
    deviceId: string;
    method: 'GET' | 'POST';
    path: string;
    body?: unknown;
    customData?: CustomData;
  }): Promise<T> {
    const request = new DataFlow.HttpRequestData();
    request.method = options.method;
    request.path = options.path;
    request.port = this.portFromData(options.customData);
    request.deviceId = options.deviceId;
    request.isSecure = false;
    request.dataType = DataFlow.DataType.JSON;
    if (options.body) {
      request.data = JSON.stringify(options.body);
    }

    const response = (await this.deviceManager.send(request)) as DataFlow.HttpResponseData;
    if (!response || !response.data) {
      throw new Error('Empty response from device');
    }
    const decoder = new TextDecoder();
    const json = decoder.decode(response.data);
    return JSON.parse(json) as T;
  }

  private async onIdentify(request: IntentFlow.IdentifyRequest): Promise<IntentFlow.IdentifyResponse> {
    console.log('[LocalHome] Identify request', JSON.stringify(request));
    const payload = request.inputs?.[0]?.payload;
    const deviceInfo = payload?.device?.deviceInfo;
    const id = payload?.device?.id || DEFAULT_DEVICE_ID;

    const device: IntentFlow.Device = {
      id,
      deviceInfo: {
        manufacturer: 'SeaMonitor',
        model: 'SeaSensor-ESP32',
        hwVersion: 'revA',
        swVersion: 'local-bridge-0.1',
      },
      type: 'action.devices.types.SENSOR',
      traits: ['action.devices.traits.SensorState', 'action.devices.traits.OnOff'],
      attributes: {
        sensorStatesSupported: [
          { name: 'airTemperatureC', unit: 'C' },
          { name: 'humidityPercent', unit: '%' },
          { name: 'waterTemperatureC', unit: 'C' },
          { name: 'waterLevelCm', unit: 'cm' },
          { name: 'airPressureHpa', unit: 'hPa' },
          { name: 'batteryPercent', unit: '%' },
        ],
      },
      customData: {
        deviceId: id,
        hostname: DEFAULT_HOSTNAME,
        port: 80,
        ...deviceInfo,
      },
    };

    return {
      intent: request.intent,
      payload: {
        device: {
          id,
          deviceInfo: device.deviceInfo,
          manufacturer: device.deviceInfo?.manufacturer,
          model: device.deviceInfo?.model,
        },
        deviceId: id,
        verificationId: id,
      },
    } as IntentFlow.IdentifyResponse;
  }

  private async onQuery(request: IntentFlow.QueryRequest): Promise<IntentFlow.QueryResponse> {
    console.log('[LocalHome] Query request', JSON.stringify(request));
    const devices = request.inputs[0]?.payload?.devices || [];
    const payload: Record<string, IntentFlow.CommandResponseStates> = {};

    for (const device of devices) {
      const customData = device.customData as CustomData | undefined;
      const deviceId = device.id || customData?.deviceId || DEFAULT_DEVICE_ID;
      try {
        const state = await this.sendHttpRequest<{ [key: string]: unknown }>({
          deviceId,
          method: 'GET',
          path: '/api/google/state',
          customData,
        });
        payload[deviceId] = {
          status: 'SUCCESS',
          ...state,
        } as IntentFlow.CommandResponseStates;
      } catch (err) {
        console.error('[LocalHome] Query failed', err);
        payload[deviceId] = {
          status: 'ERROR',
          errorCode: 'deviceOffline',
        } as IntentFlow.CommandResponseStates;
      }
    }

    return {
      requestId: request.requestId,
      payload: {
        devices: payload,
      },
    };
  }

  private async onExecute(request: IntentFlow.ExecuteRequest): Promise<IntentFlow.ExecuteResponse> {
    console.log('[LocalHome] Execute request', JSON.stringify(request));
    const commands = request.inputs[0]?.payload?.commands || [];
    const results: IntentFlow.ExecuteResponseCommands[] = [];

    for (const command of commands) {
      const ids = command.devices?.map((d) => d.id || DEFAULT_DEVICE_ID) || [DEFAULT_DEVICE_ID];
      const customData = command.devices?.[0]?.customData as CustomData | undefined;
      try {
        const body = {
          requestId: request.requestId,
          inputs: [{
            intent: 'action.devices.EXECUTE',
            payload: { commands: [command] },
          }],
        };
        await this.sendHttpRequest({
          deviceId: ids[0],
          method: 'POST',
          path: '/api/google/homegraph',
          body,
          customData,
        });
        results.push({
          ids,
          status: 'SUCCESS',
        });
      } catch (err) {
        console.error('[LocalHome] Execute failed', err);
        results.push({
          ids,
          status: 'ERROR',
          errorCode: 'deviceOffline',
        });
      }
    }

    return {
      requestId: request.requestId,
      payload: {
        commands: results,
      },
    };
  }
}

new SeaLocalHome(new App(APP_ID));
