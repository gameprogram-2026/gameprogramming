import wave
import struct
import math

def generate_siren(filename, duration, sr=44100):
    num_samples = int(sr * duration)
    with wave.open(filename, 'w') as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(sr)
        
        for i in range(num_samples):
            t = i / sr
            # Siren oscillates between 400Hz and 800Hz
            freq = 600 + 200 * math.sin(2 * math.pi * 1.5 * t)
            sample = int(math.sin(2 * math.pi * freq * t) * 10000)
            f.writeframesraw(struct.pack('<h', sample))

if __name__ == '__main__':
    generate_siren('/Users/gimseongjun/Desktop/DeadZone/assets/sounds/siren.wav', 3.0)
