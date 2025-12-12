#!/usr/bin/env python3
import sys
import numpy as np
from scipy.io import wavfile

def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <wav_file>", file=sys.stderr)
        sys.exit(1)
    
    sample_rate, samples = wavfile.read(sys.argv[1])
    
    if samples.dtype == np.int16:
        samples = samples.astype(np.float32) / 32768.0
    elif samples.dtype == np.int32:
        samples = samples.astype(np.float32) / 2147483648.0
    
    fft = np.fft.rfft(samples)
    magnitudes = np.abs(fft)
    freqs = np.fft.rfftfreq(len(samples), 1.0 / sample_rate)
    
    for freq, mag in zip(freqs, magnitudes):
        print(f"{freq:.6f},{mag:.6f}")

if __name__ == "__main__":
    main()