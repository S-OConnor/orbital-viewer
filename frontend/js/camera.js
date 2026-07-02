// camera.js — orbit camera for the Orbital LOS Viewer.
//
// createCamera(element, options) attaches pointerdown/move/up + wheel listeners
// to `element`. Dragging orbits (yaw = longitude, pitch = latitude, pitch
// clamped to +/-89 deg so the up vector never degenerates); the wheel zooms
// exponentially. The target is the origin (Earth centre). No inertia.
//
// IMPORT-SAFE: no DOM access at import time — listeners are attached only when
// createCamera() is called at runtime.

import { lookAt } from './math3.js';

const DEG = Math.PI / 180;
const PITCH_LIMIT = 89 * DEG;

/**
 * @param {EventTarget & {setPointerCapture?:Function}} element canvas to attach to
 * @param {object} [options]
 * @returns {{getEye():number[], getDistance():number, getViewMatrix():Float32Array,
 *            setDistance(d:number):void, dispose():void}}
 */
export function createCamera(element, options = {}) {
  const target = options.target || [0, 0, 0];
  const minD = options.minDistance ?? 8;
  const maxD = options.maxDistance ?? 200;
  const rotSpeed = options.rotateSpeed ?? 0.005; // radians per pixel
  const zoomSpeed = options.zoomSpeed ?? 0.0015; // exponent per wheel unit

  // Initial view: roughly latitude 20 deg, longitude 0 deg, distance 25 units.
  let yaw = options.yaw ?? 0;
  let pitch = options.pitch ?? 20 * DEG;
  let distance = clampDist(options.distance ?? 25);

  let dragging = false;
  let lastX = 0;
  let lastY = 0;
  let activePointer = null;

  function clampDist(d) {
    return Math.max(minD, Math.min(maxD, d));
  }
  function clampPitch(p) {
    return Math.max(-PITCH_LIMIT, Math.min(PITCH_LIMIT, p));
  }

  function onDown(e) {
    dragging = true;
    lastX = e.clientX;
    lastY = e.clientY;
    activePointer = e.pointerId;
    if (element.setPointerCapture) {
      try { element.setPointerCapture(e.pointerId); } catch (_) { /* ignore */ }
    }
  }

  function onMove(e) {
    if (!dragging) return;
    const dx = e.clientX - lastX;
    const dy = e.clientY - lastY;
    lastX = e.clientX;
    lastY = e.clientY;
    // Drag right -> orbit east; drag down -> tilt up toward the pole.
    yaw -= dx * rotSpeed;
    pitch = clampPitch(pitch + dy * rotSpeed);
  }

  function onUp() {
    dragging = false;
    if (element.releasePointerCapture && activePointer != null) {
      try { element.releasePointerCapture(activePointer); } catch (_) { /* ignore */ }
    }
    activePointer = null;
  }

  function onWheel(e) {
    e.preventDefault();
    // Exponential zoom: consistent multiplicative step regardless of distance.
    distance = clampDist(distance * Math.exp(e.deltaY * zoomSpeed));
  }

  element.addEventListener('pointerdown', onDown);
  element.addEventListener('pointermove', onMove);
  element.addEventListener('pointerup', onUp);
  element.addEventListener('pointercancel', onUp);
  element.addEventListener('wheel', onWheel, { passive: false });

  function getEye() {
    const cp = Math.cos(pitch);
    const sp = Math.sin(pitch);
    const cy = Math.cos(yaw);
    const sy = Math.sin(yaw);
    return [
      target[0] + distance * cp * cy,
      target[1] + distance * cp * sy,
      target[2] + distance * sp, // +Z is the north pole
    ];
  }

  return {
    getEye,
    getDistance() { return distance; },
    getViewMatrix() { return lookAt(getEye(), target, [0, 0, 1]); },
    setDistance(d) { distance = clampDist(d); },
    dispose() {
      element.removeEventListener('pointerdown', onDown);
      element.removeEventListener('pointermove', onMove);
      element.removeEventListener('pointerup', onUp);
      element.removeEventListener('pointercancel', onUp);
      element.removeEventListener('wheel', onWheel);
    },
  };
}
