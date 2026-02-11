"""Path management - CODE vs DATA separation"""
import os
import sys
from pathlib import Path
import logging

log = logging.getLogger('paths')

def get_platform():
    if 'ANDROID_ROOT' in os.environ:
        return 'android'
    elif sys.platform == 'darwin':
        return 'macos'
    elif sys.platform == 'win32':
        return 'windows'
    return 'linux'

def get_data_dir():
    """Get persistent data directory - user-accessible location"""
    platform = get_platform()
    
    if platform == 'android':
        # Use Documents folder - user has full access, survives app updates
        # Try multiple locations in order of preference
        candidates = [
            Path('/storage/emulated/0/Documents/ChatADHD'),
            Path('/storage/emulated/0/Download/ChatADHD_data'),
            Path('/sdcard/Documents/ChatADHD'),
            Path('/sdcard/ChatADHD'),
        ]
        for path in candidates:
            try:
                path.mkdir(parents=True, exist_ok=True)
                # Test write permission
                test_file = path / '.test'
                test_file.write_text('test')
                test_file.unlink()
                data_dir = path
                break
            except:
                continue
        else:
            # Fallback to code directory
            data_dir = Path(__file__).parent / 'data_local'
            data_dir.mkdir(exist_ok=True)
    elif platform == 'macos':
        data_dir = Path.home() / 'Library' / 'Application Support' / 'ChatADHD'
    elif platform == 'windows':
        data_dir = Path(os.environ.get('APPDATA', Path.home())) / 'ChatADHD'
    else:
        data_dir = Path.home() / '.config' / 'chatadhd'
    
    data_dir.mkdir(parents=True, exist_ok=True)
    (data_dir / 'data').mkdir(exist_ok=True)
    
    log.info(f"Platform: {platform}, Data: {data_dir}")
    return data_dir

def get_code_dir():
    return Path(__file__).parent.resolve()

DATA_DIR = None
CODE_DIR = None

def init():
    global DATA_DIR, CODE_DIR
    CODE_DIR = get_code_dir()
    DATA_DIR = get_data_dir()
    
    if str(CODE_DIR) not in sys.path:
        sys.path.insert(0, str(CODE_DIR))
    
    return DATA_DIR, CODE_DIR
