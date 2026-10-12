// node test_wma.mjs <isolated decoder.mjs> <generated WMAv2 wav> <reference PCM16>
import assert from "node:assert/strict";
import fs from "node:fs";
import { pathToFileURL } from "node:url";
import path from "node:path";
const [modulePath, wavPath, referencePath] = process.argv.slice(2);
const { default: init } = await import(pathToFileURL(path.resolve(modulePath)).href);
const module = await init();
const wav = fs.readFileSync(wavPath);
let data, channels, rate, bitrate, align, flags;
for (let offset = 12; offset + 8 <= wav.length;) {
  const id = wav.toString("ascii", offset, offset + 4);
  const size = wav.readUInt32LE(offset + 4), start = offset + 8;
  if (id === "fmt ") {
    channels = wav.readUInt16LE(start + 2); rate = wav.readUInt32LE(start + 4);
    bitrate = wav.readUInt32LE(start + 8) * 8; align = wav.readUInt16LE(start + 12);
    flags = wav[start + 22];
  }
  if (id === "data") data = wav.subarray(start, start + size);
  offset = start + size + (size & 1);
}
assert(data && channels && rate && align);
const input = module._malloc(Math.max(data.length, 4096));
const output = module._malloc(400000);
try {
  module.HEAPU8.set(data, input);
  const frames = module._Kisak_DecodeWmav2Ex(input, Math.floor(data.length / align), align,
    channels, rate, bitrate, output, 100000, flags);
  const reference = fs.readFileSync(referencePath);
  assert.equal(frames * channels * 2, reference.length, "reference sample count including overlap tail");
  const pcm = module.HEAP16.subarray(output / 2, output / 2 + frames * channels);
  let dot = 0, decodedEnergy = 0, referenceEnergy = 0;
  for (let i = 0; i < pcm.length; ++i) {
    const expected = reference.readInt16LE(i * 2);
    dot += pcm[i] * expected; decodedEnergy += pcm[i] * pcm[i]; referenceEnergy += expected * expected;
  }
  const correlation = dot / Math.sqrt(decodedEnergy * referenceEnergy);
  assert(correlation > 0.999, `reference correlation ${correlation}`);
  let seed = 12345678;
  for (let test = 0; test < 1000; ++test) {
    for (let byte = 0; byte < 4096; ++byte) {
      seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0;
      module.HEAPU8[input + byte] = seed >>> 24;
    }
    module._Kisak_DecodeWmav2(input, 1, test % 2 ? 2230 : 4096,
      test % 2 ? 1 : 2, [22050, 32000, 44100, 48000][test % 4], test % 2 ? 48000 : 96000,
      output, 100000);
  }
  console.log(`PASS WMAv2: ${frames} frames; correlation ${correlation.toFixed(7)}; 1000 corrupt packets without traps`);
} finally { module._free(input); module._free(output); }
