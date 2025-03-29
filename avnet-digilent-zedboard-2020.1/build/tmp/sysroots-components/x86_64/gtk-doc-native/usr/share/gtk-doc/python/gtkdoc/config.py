version = "1.31"

# tools
dblatex = ''
fop = ''
pkg_config = 'FIXMESTAGINGDIRHOST/usr/bin/pkg-config'
xsltproc = ''

# configured directories
prefix = 'FIXMESTAGINGDIRHOST/usr'
datarootdir = "${prefix}/share".replace('${prefix}', prefix)
datadir = "FIXMESTAGINGDIRHOST/usr/share".replace('${datarootdir}', datarootdir)

exeext = ''
