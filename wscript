# -*- Mode: python; py-indent-offset: 4; indent-tabs-mode: nil; coding: utf-8; -*-

from waflib import Context, Logs, Utils
import os, subprocess

VERSION = '0.1.0'
APPNAME = 'ndn-svs'
GIT_TAG_PREFIX = 'ndn-svs-'

def options(opt):
    opt.load(['compiler_cxx', 'gnu_dirs'])
    opt.load(['default-compiler-flags',
              'coverage', 'sanitizers', 'boost',
              'doxygen'],
             tooldir=['.waf-tools'])

    optgrp = opt.add_option_group('ndn-svs Options')

    optgrp.add_option('--enable-static', action='store_true', default=False,
                      dest='enable_static', help='Build static library (disabled by default)')
    optgrp.add_option('--disable-static', action='store_false', default=False,
                      dest='enable_static', help='Do not build static library (disabled by default)')

    optgrp.add_option('--enable-shared', action='store_true', default=True,
                      dest='enable_shared', help='Build shared library (enabled by default)')
    optgrp.add_option('--disable-shared', action='store_false', default=True,
                      dest='enable_shared', help='Do not build shared library (enabled by default)')

    optgrp.add_option('--with-examples', action='store_true', default=False,
                      help='Build examples')
    optgrp.add_option('--with-tests', action='store_true', default=False,
                      help='Build unit tests')

def configure(conf):
    conf.start_msg('Building static library')
    if conf.options.enable_static:
        conf.end_msg('yes')
    else:
        conf.end_msg('no', color='YELLOW')
    conf.env.enable_static = conf.options.enable_static

    conf.start_msg('Building shared library')
    if conf.options.enable_shared:
        conf.end_msg('yes')
    else:
        conf.end_msg('no', color='YELLOW')
    conf.env.enable_shared = conf.options.enable_shared

    if not conf.options.enable_shared and not conf.options.enable_static:
        conf.fatal('Either static library or shared library must be enabled')

    conf.load(['compiler_cxx', 'gnu_dirs',
               'default-compiler-flags', 'boost',
               'doxygen'])

    conf.env.WITH_EXAMPLES = conf.options.with_examples
    conf.env.WITH_TESTS = conf.options.with_tests

    conf.find_program('dot', mandatory=False)

    # Prefer pkgconf if it's installed, because it gives more correct results
    # on Fedora/CentOS/RHEL/etc. See https://bugzilla.redhat.com/show_bug.cgi?id=1953348
    # Store the result in env.PKGCONFIG, which is the variable used inside check_cfg()
    conf.find_program(['pkgconf', 'pkg-config'], var='PKGCONFIG')

    pkg_config_path = os.environ.get('PKG_CONFIG_PATH', f'{conf.env.LIBDIR}/pkgconfig')
    conf.check_cfg(package='libndn-cxx', args=['libndn-cxx >= 0.8.0', '--cflags', '--libs'],
                   uselib_store='NDN_CXX', pkg_config_path=pkg_config_path)

    boost_libs = ['system']
    if conf.env.WITH_TESTS:
        boost_libs.append('unit_test_framework')

    conf.check_boost(lib=boost_libs, mt=True)

    conf.check_compiler_flags()

    # Loading "late" to prevent tests from being compiled with profiling flags
    conf.load('coverage')
    conf.load('sanitizers')

    # If there happens to be a static library, waf will put the corresponding -L flags
    # before dynamic library flags.  This can result in compilation failure when the
    # system has a different version of the ndn-svs library installed.
    conf.env.prepend_value('STLIBPATH', ['.'])

    conf.define_cond('HAVE_TESTS', conf.env.WITH_TESTS)
    # The config header will contain all defines that were added using conf.define()
    # or conf.define_cond().  Everything that was added directly to conf.env.DEFINES
    # will not appear in the config header, but will instead be passed directly to the
    # compiler on the command line.
    conf.write_config_header('config.hpp', define_prefix='NDN_SVS_')

def build(bld):
    libndn_svs = dict(
        target='ndn-svs',
        vnum=VERSION,
        cnum=VERSION,
        source=bld.path.ant_glob('ndn-svs/**/*.cpp'),
        use='NDN_CXX BOOST',
        includes='ndn-svs .',
        export_includes='ndn-svs .',
        install_path='${LIBDIR}')

    if bld.env.enable_shared:
        bld.shlib(name='ndn-svs',
                  **libndn_svs)

    if bld.env.enable_static:
        bld.stlib(name='ndn-svs-static' if bld.env.enable_shared else 'ndn-svs',
                  **libndn_svs)

    if bld.env.WITH_TESTS:
        bld.recurse('tests')

    if bld.env.WITH_EXAMPLES:
        bld.recurse('examples')

    headers = bld.path.ant_glob('ndn-svs/**/*.hpp')
    bld.install_files('${INCLUDEDIR}', headers, relative_trick=True)

    bld.install_files('${INCLUDEDIR}/ndn-svs',
                      bld.path.find_resource('config.hpp'))

    bld(features='subst',
        source='libndn-svs.pc.in',
        target='libndn-svs.pc',
        install_path='${LIBDIR}/pkgconfig',
        VERSION=VERSION)

def doxygen(bld):
    version(bld)

    if not bld.env.DOXYGEN:
        bld.fatal('Cannot build documentation ("doxygen" not found in PATH)')

    bld(features='subst',
        name='doxygen.conf',
        source=['docs/doxygen.conf.in',
                'docs/named_data_theme/named_data_footer-with-analytics.html.in'],
        target=['docs/doxygen.conf',
                'docs/named_data_theme/named_data_footer-with-analytics.html'],
        VERSION=VERSION,
        HAVE_DOT='YES' if bld.env.DOT else 'NO',
        HTML_FOOTER='../build/docs/named_data_theme/named_data_footer-with-analytics.html' \
                        if os.getenv('GOOGLE_ANALYTICS', None) \
                        else '../docs/named_data_theme/named_data_footer.html',
        GOOGLE_ANALYTICS=os.getenv('GOOGLE_ANALYTICS', ''))

    bld(features='doxygen',
        doxyfile='docs/doxygen.conf',
        use='doxygen.conf')

def version(ctx):
    # don't execute more than once
    if getattr(Context.g_module, 'VERSION_BASE', None):
        return

    Context.g_module.VERSION_BASE = Context.g_module.VERSION
    Context.g_module.VERSION_SPLIT = VERSION_BASE.split('.')

    # first, try to get a version string from git
    gotVersionFromGit = False
    try:
        cmd = ['git', 'describe', '--always', '--match', '%s*' % GIT_TAG_PREFIX]
        out = subprocess.check_output(cmd, universal_newlines=True).strip()
        if out:
            gotVersionFromGit = True
            if out.startswith(GIT_TAG_PREFIX):
                Context.g_module.VERSION = out.lstrip(GIT_TAG_PREFIX)
            else:
                # no tags matched
                Context.g_module.VERSION = '%s-commit-%s' % (VERSION_BASE, out)
    except (OSError, subprocess.CalledProcessError):
        pass

    versionFile = ctx.path.find_node('VERSION.info')
    if not gotVersionFromGit and versionFile is not None:
        try:
            Context.g_module.VERSION = versionFile.read()
            return
        except EnvironmentError:
            pass

    # version was obtained from git, update VERSION file if necessary
    if versionFile is not None:
        try:
            if versionFile.read() == Context.g_module.VERSION:
                # already up-to-date
                return
        except EnvironmentError as e:
            Logs.warn('%s exists but is not readable (%s)' % (versionFile, e.strerror))
    else:
        versionFile = ctx.path.make_node('VERSION.info')

    try:
        versionFile.write(Context.g_module.VERSION)
    except EnvironmentError as e:
        Logs.warn('%s is not writable (%s)' % (versionFile, e.strerror))

def dist(ctx):
    ctx.algo = 'tar.xz'
    version(ctx)

def distcheck(ctx):
    ctx.algo = 'tar.xz'
    version(ctx)
