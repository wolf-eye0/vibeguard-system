/**
 * Wokwi Mock Runtime Harness
 * Headless WebAssembly simulation of the Wokwi C Chip API (wokwi-api.h)
 * 
 * Provides an authentic runtime environment for testing custom chips:
 * - Pin I/O, pull-ups, pull-downs, and pin change interrupts (pinWatch)
 * - Hardware SPI slave engine (Modes 0, 1, 2, 3, burst read/write)
 * - Microsecond/nanosecond high-resolution timers (timerInit/timerStart)
 * - Attribute sliders and controls (attrInit, attrRead, attrReadFloat)
 * - Framebuffer display subsystem (framebufferInit, bufferWrite, bufferRead)
 * - POSIX/WASI preview1 stdio hooks for printf capture
 */

const fs = require('fs');
const path = require('path');

const PIN_MODES = {
  INPUT: 0,
  OUTPUT: 1,
  INPUT_PULLUP: 2,
  INPUT_PULLDOWN: 3,
  ANALOG: 4,
  OUTPUT_LOW: 16,
  OUTPUT_HIGH: 17
};

const PIN_EDGES = {
  RISING: 1,
  FALLING: 2,
  BOTH: 3
};

class WokwiMockHarness {
  constructor(options = {}) {
    this.memory = new WebAssembly.Memory({
      initial: options.initialMemoryPages || 4,
      maximum: options.maxMemoryPages || 256
    });

    this.pins = new Map(); // id -> { id, name, mode, value }
    this.pinNameMap = new Map(); // name -> id
    this.nextPinId = 1;

    this.pinWatches = new Map(); // pinId -> { userData, edge, callbackIdx }

    this.attrs = new Map(); // id -> { id, name, value, isFloat }
    this.attrNameMap = new Map(); // name -> id
    this.nextAttrId = 1;

    this.spiDevices = new Map(); // id -> { id, userData, sck, mosi, miso, mode, doneIdx, activeBufPtr, activeCount }
    this.nextSpiId = 1;

    this.timers = new Map(); // id -> { id, userData, callbackIdx, intervalUs, targetTimeUs, repeat, active }
    this.nextTimerId = 1;

    this.framebuffers = new Map(); // id -> { id, width, height, buffer: Uint8Array }
    this.nextFbId = 1;

    this.currentTimeUs = 0;
    this.stdoutBuffer = '';

    this.table = null;
    this.instance = null;
    this.chipManifest = null;
    this.wasmPath = null;
  }

  _readStringFromMemory(ptr) {
    if (!ptr) return '';
    const view = new Uint8Array(this.memory.buffer);
    let str = '';
    let p = ptr;
    while (p < view.length && view[p] !== 0) {
      str += String.fromCharCode(view[p++]);
    }
    return str;
  }

  _writeStringToMemory(ptr, str) {
    const view = new Uint8Array(this.memory.buffer);
    for (let i = 0; i < str.length; i++) {
      view[ptr + i] = str.charCodeAt(i);
    }
    view[ptr + str.length] = 0;
  }

  buildImports() {
    const self = this;

    return {
      env: {
        memory: self.memory,

        // Pin API
        pinInit: (namePtr, mode) => {
          const name = self._readStringFromMemory(namePtr);
          const id = self.nextPinId++;
          let initVal = 0;
          if (mode === PIN_MODES.OUTPUT_HIGH || mode === PIN_MODES.INPUT_PULLUP) {
            initVal = 1;
          }
          const pinObj = { id, name, mode, value: initVal };
          self.pins.set(id, pinObj);
          self.pinNameMap.set(name, id);
          return id;
        },

        pinRead: (pinId) => {
          const pin = self.pins.get(pinId);
          return pin ? pin.value : 0;
        },

        pinWrite: (pinId, value) => {
          const pin = self.pins.get(pinId);
          if (pin) {
            const oldVal = pin.value;
            const newVal = value ? 1 : 0;
            pin.value = newVal;
            if (oldVal !== newVal) {
              self._triggerPinWatch(pinId, oldVal, newVal);
            }
          }
        },

        pinMode: (pinId, mode) => {
          const pin = self.pins.get(pinId);
          if (pin) pin.mode = mode;
        },

        pinADCRead: (pinId) => {
          const pin = self.pins.get(pinId);
          return pin ? (pin.voltage !== undefined ? pin.voltage : (pin.value ? 3.3 : 0.0)) : 0.0;
        },

        pinDACWrite: (pinId, voltage) => {
          const pin = self.pins.get(pinId);
          if (pin) {
            pin.voltage = voltage;
            pin.value = voltage >= 1.65 ? 1 : 0;
          }
          return voltage;
        },

        pinWatch: (pinId, configPtr) => {
          const dv = new DataView(self.memory.buffer);
          const userData = dv.getUint32(configPtr, true);
          const edge = dv.getUint32(configPtr + 4, true);
          const callbackIdx = dv.getUint32(configPtr + 8, true);
          self.pinWatches.set(pinId, { userData, edge, callbackIdx });
          return 1;
        },

        pinWatchStop: (pinId) => {
          self.pinWatches.delete(pinId);
        },

        // Attributes & Controls API
        attrInit: (namePtr, defVal) => {
          const name = self._readStringFromMemory(namePtr);
          const id = self.nextAttrId++;
          self.attrs.set(id, { id, name, value: defVal, isFloat: false });
          self.attrNameMap.set(name, id);
          return id;
        },

        attrRead: (attrId) => {
          const attr = self.attrs.get(attrId);
          return attr ? Math.round(attr.value) : 0;
        },

        attrReadFloat: (attrId) => {
          const attr = self.attrs.get(attrId);
          return attr ? Number(attr.value) : 0.0;
        },

        // Hardware SPI Slave API
        spiInit: (cfgPtr) => {
          const dv = new DataView(self.memory.buffer);
          const userData = dv.getUint32(cfgPtr, true);
          const sck = dv.getUint32(cfgPtr + 4, true);
          const mosi = dv.getUint32(cfgPtr + 8, true);
          const miso = dv.getUint32(cfgPtr + 12, true);
          const mode = dv.getUint32(cfgPtr + 16, true);
          const doneIdx = dv.getUint32(cfgPtr + 20, true);
          const id = self.nextSpiId++;
          self.spiDevices.set(id, {
            id,
            userData,
            sck,
            mosi,
            miso,
            mode,
            doneIdx,
            activeBufPtr: 0,
            activeCount: 0
          });
          return id;
        },

        spiStart: (spiId, bufPtr, count) => {
          const spi = self.spiDevices.get(spiId);
          if (spi) {
            spi.activeBufPtr = bufPtr;
            spi.activeCount = count;
          }
        },

        spiStop: (spiId) => {
          const spi = self.spiDevices.get(spiId);
          if (spi) {
            spi.activeBufPtr = 0;
            spi.activeCount = 0;
          }
        },

        // Timer API
        timerInit: (cfgPtr) => {
          const dv = new DataView(self.memory.buffer);
          const userData = dv.getUint32(cfgPtr, true);
          const callbackIdx = dv.getUint32(cfgPtr + 4, true);
          const id = self.nextTimerId++;
          self.timers.set(id, {
            id,
            userData,
            callbackIdx,
            intervalUs: 0,
            targetTimeUs: 0,
            repeat: false,
            active: false
          });
          return id;
        },

        timerStart: (timerId, micros, repeat) => {
          const timer = self.timers.get(timerId);
          if (timer) {
            timer.intervalUs = micros;
            timer.targetTimeUs = self.currentTimeUs + micros;
            timer.repeat = Boolean(repeat);
            timer.active = true;
          }
        },

        timerStartNanos: (timerId, nanos, repeat) => {
          const timer = self.timers.get(timerId);
          if (timer) {
            const micros = Math.max(1, Math.round(nanos / 1000));
            timer.intervalUs = micros;
            timer.targetTimeUs = self.currentTimeUs + micros;
            timer.repeat = Boolean(repeat);
            timer.active = true;
          }
        },

        timerStop: (timerId) => {
          const timer = self.timers.get(timerId);
          if (timer) timer.active = false;
        },

        getSimNanos: () => {
          return self.currentTimeUs * 1000;
        },

        // Framebuffer API
        framebufferInit: (wPtr, hPtr) => {
          const dv = new DataView(self.memory.buffer);
          let w = dv.getUint32(wPtr, true);
          let h = dv.getUint32(hPtr, true);

          if ((w === 0 || h === 0) && self.chipManifest && self.chipManifest.display) {
            w = self.chipManifest.display.width;
            h = self.chipManifest.display.height;
            dv.setUint32(wPtr, w, true);
            dv.setUint32(hPtr, h, true);
          }

          if (w === 0) w = 128;
          if (h === 0) h = 64;

          const id = self.nextFbId++;
          const fb = {
            id,
            width: w,
            height: h,
            buffer: new Uint8Array(w * h * 4)
          };
          self.framebuffers.set(id, fb);
          return id;
        },

        bufferRead: (bufId, offset, dataPtr, dataLen) => {
          const fb = self.framebuffers.get(bufId);
          if (!fb) return;
          const src = fb.buffer.subarray(offset, offset + dataLen);
          const dest = new Uint8Array(self.memory.buffer, dataPtr, dataLen);
          dest.set(src);
        },

        bufferWrite: (bufId, offset, dataPtr, dataLen) => {
          const fb = self.framebuffers.get(bufId);
          if (!fb) return;
          const src = new Uint8Array(self.memory.buffer, dataPtr, dataLen);
          fb.buffer.set(src, offset);
        }
      },

      wasi_snapshot_preview1: {
        fd_write: (fd, iovs, iovsLen, nwrittenPtr) => {
          const dv = new DataView(self.memory.buffer);
          let totalWritten = 0;
          for (let i = 0; i < iovsLen; i++) {
            const ptr = dv.getUint32(iovs + i * 8, true);
            const len = dv.getUint32(iovs + i * 8 + 4, true);
            const bytes = new Uint8Array(self.memory.buffer, ptr, len);
            const chunk = Buffer.from(bytes).toString('utf8');
            self.stdoutBuffer += chunk;
            totalWritten += len;
          }
          dv.setUint32(nwrittenPtr, totalWritten, true);
          return 0;
        },
        fd_close: () => 0,
        fd_fdstat_get: () => 0,
        fd_seek: () => 0
      }
    };
  }

  _triggerPinWatch(pinId, oldVal, newVal) {
    const watch = this.pinWatches.get(pinId);
    if (!watch || !this.table) return;

    const rising = oldVal === 0 && newVal === 1;
    const falling = oldVal === 1 && newVal === 0;

    let shouldTrigger = false;
    if (watch.edge === PIN_EDGES.BOTH) shouldTrigger = true;
    else if (watch.edge === PIN_EDGES.RISING && rising) shouldTrigger = true;
    else if (watch.edge === PIN_EDGES.FALLING && falling) shouldTrigger = true;

    if (shouldTrigger) {
      const fn = this.table.get(watch.callbackIdx);
      if (typeof fn === 'function') {
        fn(watch.userData, pinId, newVal);
      }
    }
  }

  async loadChip(wasmPath, jsonPath = null) {
    this.wasmPath = wasmPath;
    if (!fs.existsSync(wasmPath)) {
      throw new Error(`WASM file not found at: ${wasmPath}`);
    }

    if (jsonPath && fs.existsSync(jsonPath)) {
      this.chipManifest = JSON.parse(fs.readFileSync(jsonPath, 'utf8'));
    }

    const wasmBytes = fs.readFileSync(wasmPath);
    const imports = this.buildImports();

    const { instance, module } = await WebAssembly.instantiate(wasmBytes, imports);
    this.instance = instance;
    this.module = module;

    if (instance.exports.__indirect_function_table) {
      this.table = instance.exports.__indirect_function_table;
    }

    // Call chipInit
    if (typeof instance.exports.chipInit === 'function') {
      instance.exports.chipInit();
    } else {
      throw new Error(`WASM binary does not export 'chipInit' function: ${wasmPath}`);
    }

    return this;
  }

  // Pin helpers
  getPin(name) {
    const id = this.pinNameMap.get(name);
    return id ? this.pins.get(id) : null;
  }

  getPinById(id) {
    return this.pins.get(id);
  }

  getPinList() {
    return Array.from(this.pins.values());
  }

  setPin(nameOrId, value) {
    let pin;
    if (typeof nameOrId === 'string') {
      pin = this.getPin(nameOrId);
    } else {
      pin = this.pins.get(nameOrId);
    }
    if (!pin) throw new Error(`Unknown pin: ${nameOrId}`);

    const oldVal = pin.value;
    const newVal = value ? 1 : 0;
    pin.value = newVal;
    if (oldVal !== newVal) {
      this._triggerPinWatch(pin.id, oldVal, newVal);
    }
  }

  // Attribute helpers
  getAttr(name) {
    const id = this.attrNameMap.get(name);
    return id ? this.attrs.get(id) : null;
  }

  setAttr(name, value) {
    const attr = this.getAttr(name);
    if (!attr) throw new Error(`Unknown attribute: ${name}`);
    attr.value = value;
  }

  // SPI Transaction Engine
  spiSetCS(level) {
    const csPin = this.getPin('CS');
    if (!csPin) throw new Error("No 'CS' pin found on chip");
    this.setPin(csPin.id, level ? 1 : 0);
  }

  spiTransfer(bytes) {
    const spi = this.spiDevices.get(1);
    if (!spi) throw new Error('No SPI device initialized on chip');
    if (!this.table) throw new Error('Indirect function table not exported');

    const doneFn = this.table.get(spi.doneIdx);
    if (typeof doneFn !== 'function') {
      throw new Error(`Invalid done callback in SPI device: idx=${spi.doneIdx}`);
    }

    const rxBytes = [];

    // 1. Lower CS
    this.spiSetCS(0);

    for (let i = 0; i < bytes.length; i++) {
      const txByte = bytes[i];
      if (!spi.activeBufPtr) {
        throw new Error(`Chip not ready for SPI byte at index ${i}`);
      }

      // Read current MISO byte from chip's transmit buffer before writing
      const misoByte = new Uint8Array(this.memory.buffer)[spi.activeBufPtr];
      rxBytes.push(misoByte);

      // Write MOSI byte into the chip's buffer
      new Uint8Array(this.memory.buffer)[spi.activeBufPtr] = txByte;

      // Trigger done callback for 1 byte transfer
      doneFn(spi.userData, spi.activeBufPtr, 1);
    }

    // 2. Raise CS
    this.spiSetCS(1);

    return rxBytes;
  }

  // Timer helpers
  stepTimer(timerId = 1) {
    const timer = this.timers.get(timerId);
    if (!timer) throw new Error(`No timer found with ID ${timerId}`);
    if (!this.table) throw new Error('Indirect function table not exported');

    const fn = this.table.get(timer.callbackIdx);
    if (typeof fn !== 'function') {
      throw new Error(`Invalid callback for timer ${timerId}`);
    }
    fn(timer.userData);
  }

  advanceTime(micros) {
    const targetTime = this.currentTimeUs + micros;
    let guard = 0;
    while (this.currentTimeUs < targetTime && guard < 10000) {
      guard++;
      // Find the earliest active timer that should expire
      let earliest = null;
      for (const t of this.timers.values()) {
        if (t.active && t.targetTimeUs <= targetTime) {
          if (!earliest || t.targetTimeUs < earliest.targetTimeUs) {
            earliest = t;
          }
        }
      }

      if (!earliest) {
        this.currentTimeUs = targetTime;
        break;
      }

      this.currentTimeUs = earliest.targetTimeUs;
      if (!earliest.repeat) {
        earliest.active = false;
      } else {
        earliest.targetTimeUs += earliest.intervalUs;
      }

      const fn = this.table.get(earliest.callbackIdx);
      if (typeof fn === 'function') {
        fn(earliest.userData);
      }
    }
  }

  getStdout() {
    return this.stdoutBuffer;
  }

  getFramebuffer(id = 1) {
    return this.framebuffers.get(id);
  }
}

module.exports = {
  WokwiMockHarness,
  PIN_MODES,
  PIN_EDGES
};
