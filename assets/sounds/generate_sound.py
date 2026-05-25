import wave
import struct
import random
import math

sample_rate = 44100
duration = 0.15 # 150ms
num_samples = int(sample_rate * duration)

with wave.open('/Users/gimseongjun/Desktop/DeadZone/assets/sounds/footstep.wav', 'w') as f:
    f.setnchannels(1) # mono
    f.setsampwidth(2) # 16-bit
    f.setframerate(sample_rate)
    
    for i in range(num_samples):
        t = i / sample_rate
        # Envelope: fast attack, exponential decay
        envelope = math.exp(-25.0 * t)
        
        # Base thump (low frequency sine wave that sweeps down)
        freq = 150.0 * math.exp(-30.0 * t) + 40.0
        thump = math.sin(2.0 * math.pi * freq * t)
        
        # Dirt/Gravel texture (filtered noise, decreasing rapidly)
        noise = random.uniform(-1.0, 1.0)
        gravel = noise * math.exp(-40.0 * t) * 0.4
        
        # Combine and apply volume
        combined = thump * 0.6 + gravel
        sample = int(combined * envelope * 12000)
        
        # Clip to 16-bit
        sample = max(-32768, min(32767, sample))
        f.writeframesraw(struct.pack('<h', sample))

print("High quality footstep generated.")
