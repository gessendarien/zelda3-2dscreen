import sys
try:
    from PIL import Image, ImageOps
    # Load the new banner image
    img = Image.open('banner/banner_image.png')
    
    # Convert to RGBA to allow transparent padding
    img = img.convert("RGBA")
    
    # Pad the image to fit 256x128 without cropping or stretching.
    # It will add transparent borders where necessary.
    img = ImageOps.pad(img, (256, 128), color=(0, 0, 0, 0), method=Image.LANCZOS)
    
    img.save('banner_image.png')
    print("Resized successfully with padding")
except Exception as e:
    print(f"Error: {e}")
    sys.exit(1)
