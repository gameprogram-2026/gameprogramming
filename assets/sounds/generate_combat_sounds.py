import wave
import struct
import random
import math

def generate_sound(filename, duration, gen_sample):
    sample_rate = 44100
    num_samples = int(sample_rate * duration)
    with wave.open(filename, 'w') as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(sample_rate)
        for i in range(num_samples):
            sample = gen_sample(i, num_samples, sample_rate)
            # clamp
            sample = max(-32768, min(32767, int(sample)))
            f.writeframesraw(struct.pack('<h', sample))

# 1. Shoot (loud, sharp attack, exponential decay)
def gen_shoot(i, num_samples, sr):
    t = i / sr
    env = math.exp(-t * 20)
    noise = random.uniform(-1.0, 1.0)
    return noise * env * 25000

# 2. Swing (swoosh, slow attack and decay)
def gen_swing(i, num_samples, sr):
    t = i / num_samples
    env = math.sin(t * math.pi) ** 2
    noise = random.uniform(-1.0, 1.0)
    # low pass filter approximation
    return noise * env * 6000

# 3. Hit (short, punchy low freq + noise)
def gen_hit(i, num_samples, sr):
    t = i / sr
    env = math.exp(-t * 30)
    freq = max(50, 200 - t * 2000)
    osc = math.sin(2 * math.pi * freq * t)
    noise = random.uniform(-1.0, 1.0) * 0.3
    return (osc + noise) * env * 20000

# 4. Siren
def gen_siren(i, num_samples, sr):
    t = i / sr
    # Siren modulates frequency up and down
    freq = 600 + 200 * math.sin(2 * math.pi * 0.5 * t)
    osc = math.sin(2 * math.pi * freq * t)
    # Slow attack and decay over the whole duration
    env = math.sin((i / num_samples) * math.pi)
    return osc * env * 15000

generate_sound('/Users/gimseongjun/Desktop/DeadZone/assets/sounds/shoot.wav', 0.4, gen_shoot)
generate_sound('/Users/gimseongjun/Desktop/DeadZone/assets/sounds/swing.wav', 0.3, gen_swing)
generate_sound('/Users/gimseongjun/Desktop/DeadZone/assets/sounds/hit.wav', 0.2, gen_hit)
generate_sound('/Users/gimseongjun/Desktop/DeadZone/assets/sounds/siren.wav', 3.0, gen_siren)
