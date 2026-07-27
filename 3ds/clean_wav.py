import wave
import sys
import array

try:
    with wave.open('banner/audio-theme.wav', 'rb') as in_wav:
        params = in_wav.getparams()
        # Max 3 seconds
        max_frames = int(params.framerate * 3)
        nframes = min(in_wav.getnframes(), max_frames)
        frames = in_wav.readframes(nframes)
        
        # update params to reflect the new number of frames
        params = params._replace(nframes=nframes)

    # Apply fade out to the last 1 second
    samples = array.array('h', frames)
    fade_seconds = 1.0
    fade_frames = int(params.framerate * fade_seconds)
    
    start_fade = nframes - fade_frames
    if start_fade < 0:
        start_fade = 0
        fade_frames = nframes
        
    for i in range(start_fade, nframes):
        multiplier = 1.0 - ((i - start_fade) / fade_frames)
        for ch in range(params.nchannels):
            idx = i * params.nchannels + ch
            samples[idx] = int(samples[idx] * multiplier)

    with wave.open('clean_audio.wav', 'wb') as out_wav:
        out_wav.setparams(params)
        out_wav.writeframes(samples.tobytes())
    print("Cleaned, truncated and faded audio successfully")
except Exception as e:
    print(f"Error: {e}")
    sys.exit(1)
