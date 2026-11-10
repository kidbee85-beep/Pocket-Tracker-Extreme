#!/usr/bin/env python3
"""Optional computer-side first-run preparation. Python3 only; never overwrite an existing preset."""
from pathlib import Path
import argparse,ctypes,errno,hashlib,os,sys,tempfile


def digest(path):
    return hashlib.sha256(path.read_bytes()).digest()


def commit_new_file(temp, target):
    """Publish without replacement; stop if the mounted filesystem cannot do it."""
    try:
        _commit_new_file(temp, target)
    except OSError as error:
        if error.errno in (errno.ENOTSUP, errno.EOPNOTSUPP, errno.ENOSYS):
            raise RuntimeError(
                'This mounted filesystem does not support the atomic no-replace '
                'operation required for computer preparation. Existing presets '
                'and the pack marker have been preserved. Safely eject the card '
                'and start TrackWerk on the device; its first launch '
                'will prepare missing presets.'
            ) from error
        raise


def _commit_new_file(temp, target):
    """Use only operations that atomically refuse to replace an existing file."""
    if os.name=='nt':
        os.rename(temp,target)  # Windows rename fails if the destination exists.
        return
    libc=ctypes.CDLL(None,use_errno=True)
    if sys.platform=='darwin' and hasattr(libc,'renamex_np'):
        call=libc.renamex_np
        call.argtypes=[ctypes.c_char_p,ctypes.c_char_p,ctypes.c_uint]
        result=call(os.fsencode(temp),os.fsencode(target),4)  # RENAME_EXCL
    elif hasattr(libc,'renameat2'):
        call=libc.renameat2
        call.argtypes=[ctypes.c_int,ctypes.c_char_p,ctypes.c_int,ctypes.c_char_p,ctypes.c_uint]
        result=call(-100,os.fsencode(temp),-100,os.fsencode(target),1)  # RENAME_NOREPLACE
    else:
        # Atomic no-clobber fallback for filesystems with hard-link support.
        os.link(temp,target)
        temp.unlink()
        return
    if result:
        error=ctypes.get_errno()
        raise OSError(error,os.strerror(error),str(target))


def check_parents(root, relative):
    current=root
    if current.is_symlink():raise RuntimeError('Data/preset root is a symbolic link; preparation stopped.')
    for part in relative.parts[:-1]:
        current=current/part
        if current.is_symlink():raise RuntimeError('Preset subfolder is a symbolic link; preparation stopped.')
        if current.exists() and not current.is_dir():raise RuntimeError('A preset parent is not a directory.')


def prepare(factory, data_dir):
    factory=Path(factory)
    data_dir=Path(data_dir)
    if factory.is_symlink() or not factory.is_dir():raise RuntimeError('Factory preset directory is missing or linked.')
    expected=(factory.parent/'presets-pack.id').read_text().strip()
    if len(expected)!=64:raise RuntimeError('Factory pack identifier is invalid.')
    entries=[];pack=hashlib.sha256()
    # Verify all sources before touching the destination.
    for path in sorted(factory.rglob('*'),key=lambda p:p.relative_to(factory).as_posix()):
        if path.is_symlink():raise RuntimeError('Linked factory content is not supported.')
        if not path.is_file() or path.name.startswith('.'):continue
        relative=path.relative_to(factory);value=digest(path)
        pack.update(relative.as_posix().encode()+b'\0');pack.update(value)
        entries.append((path,relative,value))
    if pack.hexdigest()!=expected:raise RuntimeError('Factory pack verification failed; no files were copied.')
    if data_dir.is_symlink():raise RuntimeError('Data directory must not be a symbolic link.')
    destination=data_dir/'Presets'
    for _,relative,_ in entries:check_parents(destination,relative)
    destination.mkdir(parents=True,exist_ok=True)
    added=0;preserved=0
    for source,relative,expected_digest in entries:
        target=destination/relative
        check_parents(destination,relative)
        if target.is_symlink() or target.exists():
            if target.is_dir():raise RuntimeError('A preset target is a directory.')
            preserved+=1
            continue
        target.parent.mkdir(parents=True,exist_ok=True)
        fd,name=tempfile.mkstemp(prefix='.pte-preset-',dir=target.parent)
        temp=Path(name)
        try:
            with os.fdopen(fd,'wb') as output:output.write(source.read_bytes())
            if digest(temp)!=expected_digest:raise RuntimeError('A copied preset failed verification.')
            try:commit_new_file(temp,target)
            except FileExistsError:
                preserved+=1
                continue
            added+=1
        finally:
            if temp.exists():temp.unlink()
        if added%256==0:print('Prepared',added,'missing presets',flush=True)
    # Flush completed copies before committing the pack marker. This is setup,
    # never an operation in the playback/audio thread.
    if hasattr(os,'sync'):os.sync()
    marker=data_dir/'.factory-presets-pack.id'
    fd,name=tempfile.mkstemp(prefix='.pte-pack-',dir=data_dir)
    temp=Path(name)
    try:
        with os.fdopen(fd,'w') as output:
            output.write(expected+'\n');output.flush();os.fsync(output.fileno())
        os.replace(temp,marker)
    finally:
        if temp.exists():temp.unlink()
    return {'added':added,'preserved':preserved,'factory_files':len(entries),'pack_id':expected}


def main():
    here=Path(__file__).resolve()
    candidates=[here.parents[1]/'factory/Presets',here.parents[2]/'factory/Presets']
    default=next((p for p in candidates if p.is_dir()),None)
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--factory',type=Path,default=default)
    parser.add_argument('--data',type=Path)
    args=parser.parse_args()
    if args.factory is None:parser.error('Use --factory to select the verified factory/Presets folder.')
    data_dir=args.data or args.factory.parent.parent/'data'
    try:
        result=prepare(args.factory,data_dir)
        print('Ready. New files:',result['added'],'Existing files kept:',result['preserved'])
        print('Data folder:',data_dir)
    except (OSError,RuntimeError,ValueError) as error:
        print('Preparation stopped:',error,file=sys.stderr)
        return 1
    return 0


if __name__=='__main__':raise SystemExit(main())
