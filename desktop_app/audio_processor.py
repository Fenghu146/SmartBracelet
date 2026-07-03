"""
Audio processor for SmartBracelet voice assistant.
IMA ADPCM decode, WAV generation, Whisper ASR API integration.
"""

import base64
import io
import json
import struct
import time
import urllib.request
import urllib.error
import wave
from typing import Optional

# ============================================================
# IMA ADPCM decoder
# ============================================================

IMA_STEP_TABLE = [
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
    19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
    130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
    337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
    2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
    5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635,
    13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086,
    29794, 32767,
]

IMA_INDEX_TABLE = [-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8]


class ADPCMState:
    __slots__ = ('predictor', 'index')

    def __init__(self, predictor: int = 0, index: int = 0):
        self.predictor = predictor
        self.index = index


def adpcm_decode(data: bytes) -> list[int]:
    """Decode IMA ADPCM bytes to 16-bit PCM samples."""
    state = ADPCMState()
    samples = []
    for byte in data:
        for nibble in (byte & 0x0F, (byte >> 4) & 0x0F):
            step = IMA_STEP_TABLE[state.index]
            delta = step >> 3
            if nibble & 4:
                delta += step
            if nibble & 2:
                delta += step >> 1
            if nibble & 1:
                delta += step >> 2

            if nibble & 8:
                state.predictor -= delta
            else:
                state.predictor += delta

            state.predictor = max(-32768, min(32767, state.predictor))
            state.index = max(0, min(88, state.index + IMA_INDEX_TABLE[nibble]))
            samples.append(state.predictor)
    return samples


# ============================================================
# PCM → WAV
# ============================================================

def pcm_to_wav(pcm_samples: list[int], sample_rate: int = 16000) -> bytes:
    """Create a WAV file in memory from PCM samples (16-bit mono)."""
    buf = io.BytesIO()
    with wave.open(buf, 'wb') as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)  # 16-bit
        wav.setframerate(sample_rate)
        wav.writeframes(struct.pack(f'<{len(pcm_samples)}h', *pcm_samples))
    return buf.getvalue()


# ============================================================
# Whisper ASR API
# ============================================================

def transcribe_audio(
    wav_bytes: bytes,
    api_key: str = "",
    base_url: str = "https://api.openai.com/v1",
    model: str = "whisper-1",
    language: str = "zh",
) -> Optional[str]:
    """Send WAV audio to OpenAI-compatible Whisper API for transcription.

    Uses urllib (stdlib) to avoid adding requests dependency.
    Returns transcribed text, or None on failure.
    """
    try:
        # Build multipart/form-data manually (avoid external lib)
        boundary = f"----VoiceAsrBoundary{int(time.time() * 1000):x}"

        parts = []
        # Model field
        parts.append(f"--{boundary}\r\n"
                     f'Content-Disposition: form-data; name="model"\r\n\r\n'
                     f"{model}\r\n")
        # Language field
        parts.append(f"--{boundary}\r\n"
                     f'Content-Disposition: form-data; name="language"\r\n\r\n'
                     f"{language}\r\n")
        # Response format
        parts.append(f"--{boundary}\r\n"
                     f'Content-Disposition: form-data; name="response_format"\r\n\r\n'
                     f"json\r\n")
        # Audio file
        parts.append(f"--{boundary}\r\n"
                     f'Content-Disposition: form-data; name="file"; filename="audio.wav"\r\n'
                     f"Content-Type: audio/wav\r\n\r\n")
        body = "".join(parts).encode("utf-8")
        body += wav_bytes
        body += f"\r\n--{boundary}--\r\n".encode("utf-8")

        url = f"{base_url.rstrip('/')}/audio/transcriptions"
        req = urllib.request.Request(url, data=body, method="POST")
        req.add_header("Content-Type", f"multipart/form-data; boundary={boundary}")
        if api_key:
            req.add_header("Authorization", f"Bearer {api_key}")

        with urllib.request.urlopen(req, timeout=30) as resp:
            result = json.loads(resp.read().decode("utf-8"))
            return result.get("text", "")

    except (urllib.error.URLError, json.JSONDecodeError, OSError) as e:
        return None
