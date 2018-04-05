from _common import iterpair, listid, pathid, rootrel_arc_src, tobuilddir, filter_out_by_keyword


def split(lst, limit):
    # paths are specified with replaceable prefix
    # real length is unknown at the moment, that why we use root_lenght
    # as a rough estimation
    root_lenght = 200
    filepath = None
    lenght = 0
    bucket = []

    for item in lst:
        if filepath:
            lenght += root_lenght + len(filepath) + len(item)
            if lenght > limit and bucket:
                yield bucket
                bucket = []
                lenght = 0

            bucket.append(filepath)
            bucket.append(item)
            filepath = None
        else:
            filepath = item

    if bucket:
        yield bucket


def onfat_resource(unit, *args):
    unit.onpeerdir(['library/resource'])

    # Since the maximum length of lpCommandLine string for CreateProcess is 8kb (windows) characters,
    # we make several calls of rescompiler
    # https://msdn.microsoft.com/ru-ru/library/windows/desktop/ms682425.aspx
    for part_args in split(args, 8000):
        output = listid(part_args) + '.cpp'
        inputs = [x for x, y in iterpair(part_args) if x != '-']
        if inputs:
            inputs = ['IN'] + inputs

        unit.onrun_program(['tools/rescompiler', output] + part_args + inputs + ['OUT_NOAUTO', output])
        unit.onsrcs(['GLOBAL', output])


def onfrom_sandbox(unit, *args):
    """
    @usage: FROM_SANDBOX([FILE] resource_id OUT_[NOAUTO] <files from resource>)
    Downloads the archive from sandbox by resource number, unpack (if the FILE keyword is not specified), and add the files with the specified names to the assembly.
    If the files do not compile, then you need to use the parameter OUT_NOAUTO.

    What is Sandbox?
    - General-purpose distributed task execution system (internal service).
    """
    unit.onsetup_from_sandbox(filter_out_by_keyword(list(args), 'AUTOUPDATED'))
    res_id = args[0]
    if res_id == "FILE":
        res_id = args[1]
    unit.onadd_check(["check.resource", res_id])


def onresource_files(unit, *args):
    """
    RESOURCE_FILES([PREFIX {prefix}] {path}) expands into
    RESOURCE({path} resfs/file/{prefix}{path}
        - resfs/src/resfs/file/{prefix}{path}={rootrel_arc_src(path)}
    )

    resfs/src/{key} stores a source root (or build root) relative path of the
    source of the value of the {key} resource.

    resfs/file/{key} stores any value whose source was a file on a filesystem.
    resfs/src/resfs/file/{key} must store its path.

    This form is for use from other plugins:
    RESOURCE_FILES([DEST {dest}] {path}) expands into RESOURCE({path} resfs/file/{dest})
    """
    prefix = ''
    dest = None
    res = []

    args = iter(args)
    for arg in args:
        if arg == 'PREFIX':
            prefix, dest = next(args), None
        elif arg == 'DEST':
            dest, prefix = next(args), None
        else:
            path = arg
            key = 'resfs/file/' + (dest or (prefix + path))
            src = 'resfs/src/{}={}'.format(key, rootrel_arc_src(path, unit))
            res += ['-', src, path, key]

    unit.onresource(res)
