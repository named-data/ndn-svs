# -*- Mode: python; py-indent-offset: 4; indent-tabs-mode: nil; coding: utf-8; -*-

from waflib import Context, Logs, Utils
import os, subprocess

VERSION = '0.0.1'
APPNAME = 'ndn-svs'
PACKAGE_BUGREPORT = 'https://github.com/pulsejet/ndn-svs/'
PACKAGE_URL = 'https://github.com/pulsejet/ndn-svs/'
GIT_TAG_PREFIX = 'ndn-svs-'

def options(opt):
    opt.load(['compiler_cxx', 'gnu_dirs'])
    opt.load(['default-compiler-flags',
              'coverage', 'sanitizers', 'boost',
              'doxygen', 'sphinx_build'],
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

    optgrp.add_option('--with-tests', action='store_true', default=False,
                      help='Build unit tests')
    optgrp.add_option('--with-examples', action='store_true', default=False,
                      help='Build examples')

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
               'doxygen', 'sphinx_build'])

    conf.env.WITH_TESTS = conf.options.with_tests
    conf.env.WITH_EXAMPLES = conf.options.with_examples

    conf.check_cfg(package='libndn-cxx', args=['--cflags', '--libs'], uselib_store='NDN_CXX',
                   pkg_config_path=os.environ.get('PKG_CONFIG_PATH', '%s/pkgconfig' % conf.env.LIBDIR))

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

    conf.define_cond('NDN_SVS_HAVE_TESTS', conf.env.WITH_TESTS)

    # The config header will contain all defines that were added using conf.define()
    # or conf.define_cond().  Everything that was added directly to conf.env.DEFINES
    # will not appear in the config header, but will instead be passed directly to the
    # compiler on the command line.
    conf.write_config_header('config.hpp')

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

    bld.install_files(
        dest = '%s/ndn-svs' % bld.env.INCLUDEDIR,
        files = bld.path.ant_glob(['ndn-svs/*.hpp', 'common.hpp']),
        cwd = bld.path.find_dir('ndn-svs'),
        relative_trick = False)

    bld.install_files(
        dest = '%s/ndn-svs' % bld.env.INCLUDEDIR,
        files = bld.path.get_bld().ant_glob(['ndn-svs/*.hpp', 'common.hpp', 'config.hpp']),
        cwd = bld.path.get_bld().find_dir('ndn-svs'),
        relative_trick = False)

    bld(features='subst',
        source='libndn-svs.pc.in',
        target='libndn-svs.pc',
        install_path='${LIBDIR}/pkgconfig',
        VERSION=VERSION)

def docs(bld):
    from waflib import Options
    Options.commands = ['doxygen', 'sphinx'] + Options.commands

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
        HTML_FOOTER='../build/docs/named_data_theme/named_data_footer-with-analytics.html' \
                        if os.getenv('GOOGLE_ANALYTICS', None) \
                        else '../docs/named_data_theme/named_data_footer.html',
        GOOGLE_ANALYTICS=os.getenv('GOOGLE_ANALYTICS', ''))

    bld(features='doxygen',
        doxyfile='docs/doxygen.conf',
        use='doxygen.conf')

def sphinx(bld):
    version(bld)

    if not bld.env.SPHINX_BUILD:
        bld.fatal('Cannot build documentation ("sphinx-build" not found in PATH)')

    bld(features='sphinx',
        config='docs/conf.py',
        outdir='docs',
        source=bld.path.ant_glob('docs/**/*.rst'),
        version=VERSION_BASE,
        release=VERSION)

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
    version(ctx)

def distcheck(ctx):
    version(ctx)
