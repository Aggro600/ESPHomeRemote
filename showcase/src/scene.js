// Renderer, Kamera, Steuerung, Licht und Boden.
import * as THREE from "three";
import { OrbitControls } from "three/addons/controls/OrbitControls.js";
import { RoomEnvironment } from "three/addons/environments/RoomEnvironment.js";

export function szeneBauen(container) {
  const renderer = new THREE.WebGLRenderer({ antialias: true, alpha: true, powerPreference: "high-performance" });
  renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
  renderer.outputColorSpace = THREE.SRGBColorSpace;
  renderer.toneMapping = THREE.ACESFilmicToneMapping;
  renderer.toneMappingExposure = 1.05;
  renderer.shadowMap.enabled = true;
  renderer.shadowMap.type = THREE.PCFSoftShadowMap;
  container.appendChild(renderer.domElement);

  const scene = new THREE.Scene();
  const pmrem = new THREE.PMREMGenerator(renderer);
  scene.environment = pmrem.fromScene(new RoomEnvironment(), 0.04).texture;
  scene.environmentIntensity = 0.55;

  const camera = new THREE.PerspectiveCamera(32, 1, 0.005, 10);
  camera.position.set(0.32, 0.24, 0.42);

  const controls = new OrbitControls(camera, renderer.domElement);
  controls.target.set(0, 0.09, 0);
  controls.enableDamping = true;
  controls.dampingFactor = 0.08;
  controls.minDistance = 0.09;
  controls.maxDistance = 1.4;
  controls.maxPolarAngle = Math.PI * 0.94;
  controls.screenSpacePanning = true;
  controls.autoRotateSpeed = 0.9;

  // Licht: weiches Hauptlicht mit Schatten, kuehles Gegenlicht fuer Kanten, warmes Fuelllicht
  scene.add(new THREE.HemisphereLight(0xcfd8ff, 0x101216, 0.35));
  const haupt = new THREE.DirectionalLight(0xffffff, 2.6);
  haupt.position.set(0.35, 0.6, 0.45);
  haupt.castShadow = true;
  haupt.shadow.mapSize.set(2048, 2048);
  haupt.shadow.camera.left = -0.25; haupt.shadow.camera.right = 0.25;
  haupt.shadow.camera.top = 0.3; haupt.shadow.camera.bottom = -0.2;
  haupt.shadow.camera.near = 0.1; haupt.shadow.camera.far = 1.6;
  haupt.shadow.bias = -0.0004;
  haupt.shadow.normalBias = 0.002;
  haupt.shadow.radius = 4;
  scene.add(haupt);
  const kante = new THREE.DirectionalLight(0x7aa2ff, 1.6);
  kante.position.set(-0.5, 0.35, -0.55);
  scene.add(kante);
  const fuell = new THREE.DirectionalLight(0xffd9b0, 0.5);
  fuell.position.set(-0.4, 0.15, 0.5);
  scene.add(fuell);

  // Boden: nimmt nur Schatten auf, darunter ein weicher Lichtfleck
  const boden = new THREE.Mesh(new THREE.PlaneGeometry(3, 3), new THREE.ShadowMaterial({ opacity: 0.42 }));
  boden.rotation.x = -Math.PI / 2;
  boden.position.y = -0.003;
  boden.receiveShadow = true;
  scene.add(boden);
  const fleck = new THREE.Mesh(new THREE.CircleGeometry(0.42, 64), new THREE.MeshBasicMaterial({ map: lichtfleck(), transparent: true, depthWrite: false }));
  fleck.rotation.x = -Math.PI / 2;
  fleck.position.y = -0.0031;
  scene.add(fleck);

  function groesse() {
    const w = container.clientWidth, h = container.clientHeight;
    renderer.setSize(w, h, false);
    camera.aspect = w / h;
    // Hochformat: Bild nach oben schieben, unten liegt die Steuerung
    // Breit: Bildmitte nach links, rechts liegt die Steuerung (300 px + Rand)
    if (w / h < 0.8) camera.setViewOffset(w, h, 0, h * 0.14, w, h);
    else if (w > 760) camera.setViewOffset(w, h, 158, 0, w, h);
    else camera.clearViewOffset();
    camera.updateProjectionMatrix();
  }
  new ResizeObserver(groesse).observe(container);
  groesse();

  return { renderer, scene, camera, controls };
}

function lichtfleck() {
  const c = document.createElement("canvas");
  c.width = c.height = 256;
  const g = c.getContext("2d"), r = g.createRadialGradient(128, 128, 0, 128, 128, 128);
  r.addColorStop(0, "rgba(90,120,200,0.20)");
  r.addColorStop(0.5, "rgba(60,80,140,0.08)");
  r.addColorStop(1, "rgba(0,0,0,0)");
  g.fillStyle = r;
  g.fillRect(0, 0, 256, 256);
  const t = new THREE.CanvasTexture(c);
  t.colorSpace = THREE.SRGBColorSpace;
  return t;
}
