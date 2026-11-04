// UI ⇄ core JSON protocol (docs/PROTOCOL.md — binding).

export type GlobalId = 'masterTune' | 'midiChannel' | 'omni' | 'sysInfo' | 'protectInt' | 'protectCrt' | 'volume';
export type SettingId = 'engine' | 'dac' | 'hostRate' | 'classicVelocity' | 'keyboardPlay' | 'zoom' | 'opOrder';
export type BuildKind = 'web' | 'standalone' | 'plugin';
export type MidiState = 'ok' | 'unavailable' | 'nosysex';
export type PanelModeName = 'PLAY' | 'EDIT' | 'EDIT_NAME' | 'FUNCTION' | 'SPLASH';

// ---- Commands (UI → core / shell) ---------------------------------------
export type Command =
  | { t: 'noteOn'; note: number; vel: number }
  | { t: 'noteOff'; note: number }
  | { t: 'cc'; cc: number; v: number }
  | { t: 'bend'; v: number }
  | { t: 'releaseAll' }
  | { t: 'button'; id: number; down: boolean }
  | { t: 'dataEntry'; pos: number }
  | { t: 'setParam'; i: number; v: number }
  | { t: 'setFunc'; i: number; v: number }
  | { t: 'setGlobal'; id: GlobalId; v: number }
  | { t: 'setSetting'; id: SettingId; v: number }
  | { t: 'selectOp'; op: number }
  | { t: 'writeOp'; op: number; bytes: number[] }
  | { t: 'swapOps'; a: number; b: number }
  | { t: 'egCopy'; from: number; to: number }
  | { t: 'initVoice' }
  | { t: 'randomise'; seed: number }
  | { t: 'compare' }
  | { t: 'selectVoice'; bank: number; slot: number }
  | { t: 'copyVoice'; fromBank: number; fromSlot: number; toBank: number; toSlot: number }
  | { t: 'newCartridge' }
  | { t: 'ejectCartridge' }
  | { t: 'export'; what: 'int' | 'crt' | 'voice' }
  | { t: 'sendBank' }
  | { t: 'requestDump' }
  | { t: 'importFile'; name: string; b64: string }
  | { t: 'importChoice'; target: 'crt' | 'int' }
  | { t: 'importCancel' }
  | { t: 'snapshot' }
  // Shell-only
  | { t: 'pickFile' }
  | { t: 'openDevices' }
  | { t: 'midiSelect'; inputs: string[]; output: string | null }
  | { t: 'uiReady' }
  | { t: 'editorClosing' };

// ---- Events (core / shell → UI) ------------------------------------------
export interface MidiPort {
  id: string;
  name: string;
  on?: boolean;
}

export type NoticeCode =
  | 'protected'
  | 'rejected'
  | 'dumpReceived'
  | 'completed'
  | 'saveError'
  | 'writeError'
  | 'noReply';

export type VdxEvent =
  | { e: 'param'; i: number; v: number; s: string; src?: string }
  | { e: 'func'; i: number; v: number; s: string; src?: string }
  | { e: 'global'; id: GlobalId; v: number; s: string }
  | { e: 'setting'; id: SettingId; v: number }
  | { e: 'lcd'; l1: string; l2: string; row: number; col: number; cursor: boolean; lit: boolean }
  | { e: 'led'; v: number; dot: boolean }
  | { e: 'lamps'; num: number; mode: number; blink: number }
  | {
      e: 'panel';
      mode: PanelModeName;
      page: number;
      op: number;
      comparing: boolean;
      edited: boolean;
      dialog: boolean;
      message: boolean;
      armed: boolean;
    }
  | { e: 'dataEntry'; pos: number }
  | { e: 'wheels'; bend: number; mod: number }
  | { e: 'voiceName'; bank: number; slot: number; name: string }
  | { e: 'cartridge'; present: boolean; changed: boolean; name: string }
  | { e: 'memory'; selected: number; srcBank: number; srcSlot: number; edited: boolean }
  | { e: 'opFreq'; op: number; s: string }
  | { e: 'lfoHz'; s: string }
  | { e: 'meter'; db: number[] }
  | { e: 'notice'; code: NoticeCode; bank?: number; name?: string; text?: string }
  | { e: 'import'; kind: 'voice' | 'bank' | 'reject'; name: string }
  | { e: 'snapshotEnd' }
  | {
      e: 'env';
      build: BuildKind;
      midi: MidiState;
      inputs?: MidiPort[];
      outputs?: MidiPort[];
      output?: string | null;
      idb?: boolean;
      audio?: boolean;
    };
