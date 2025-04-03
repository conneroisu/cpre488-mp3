FROM ubuntu:20.04

# Define build arguments for installer customization
ARG PETALINUX_INSTALLER=petalinux-v2020.1-final-installer.run
ARG PETALINUX_VERSION=2020.1
ARG PETALINUX_RELEASE=final

# Set environment variables for non-interactive installation
ENV DEBIAN_FRONTEND=noninteractive

# Create a symbolic link so bash is used instead of dash
RUN ln -sf /bin/bash /bin/sh

# Create a symbolic link so make is used instead of gmake
RUN ln -s /usr/bin/make /usr/bin/gmake

# Add support for 32 bit Architecture on 64 bit Architecture
RUN dpkg --add-architecture i386

# Install all Vitis/Vivado/XRT/Petalinux dependancies
RUN apt-get update && apt-get install -y \
    libstdc++6:i386 \
    libgtk2.0-0:i386 \
    libtinfo5 \
    libncurses5 \
    iproute2 \
    gawk \
    gcc \
    net-tools \
    libncurses5-dev \
    zlib1g-dev:i386 \
    libssl-dev \
    flex \
    bison \
    xterm \
    autoconf \
    libtool \
    texinfo \
    zlib1g-dev \
    gcc-multilib \
    build-essential \
    automake \
    screen \
    pax \
    gcc \
    python3-pip \
    xz-utils \
    cpp \
    python3-git \
    python3-jinja2 \
    python3-pexpect \
    diffutils \
    debianutils \
    iputils-ping \
    libegl1-mesa \
    libsdl1.2-dev \
    pylint3 \
    python3 \
    cpio \
    tftpd-hpa \
    gnupg \
    git \
    git-core \
    diffstat \
    chrpath \
    socat \
    tar \
    unzip \
    python \
    make \
    gzip \
    libselinux1 \
    wget \
    tofrodos \
    libfontconfig1:i386 \
    libx11-6:i386 \
    libxext6:i386 \
    libxrender1:i386 \
    libsm6:i386 \
    xinetd \
    ncurses-dev \
    openssl \
    putty \
    g++ \
    haveged \
    perl \
    liberror-perl \
    mtd-utils \
    xtrans-dev \
    libxcb-randr0-dev \
    libxcb-xtest0-dev \
    libxcb-xinerama0-dev \
    libxcb-shape0-dev \
    libxcb-xkb-dev \
    openssh-server \
    util-linux \
    sysvinit-utils \
    cython \
    google-perftools \
    libncurses5-dev \
    libncursesw5-dev \
    libncurses5:i386 \
    dpkg-dev:i386 \
    vim \
    locales \
    rsync \
    expect \
    bc \
    ncurses-dev:i386 \
    libstdc\+\+6:i386 \
    libselinux1:i386 \
    lib32ncurses5-dev \
    && apt-get clean

RUN locale-gen en_US.UTF-8
ENV LANG=en_US.UTF-8
ENV LANGUAGE=en_US:en
ENV LC_ALL=en_US.UTF-8


# Setup bash as the default shell
SHELL ["/bin/bash", "-c"]


# Make tftp file server 
RUN mkdir -p /etc/xinetd.d && \
    cat <<EOF > /etc/xinetd.d/tftp
service tftp
{
    protocol = udp
    port = 69
    socket_type = dgram
    wait = yes
    user = nobody
    server = /usr/sbin/in.tftpd
    server_args = /tftpboot
    disable = no
}
EOF

#Make the TFTP server for petalinux
RUN mkdir /tftpboot && \
    chmod -R 777 /tftpboot && \
    chown -R nobody /tftpboot && \
    /etc/init.d/xinetd stop && \
    /etc/init.d/xinetd start

# As root user copy and change permissions of petlinux installer 
# Check if the installer file exists and has content
COPY ${PETALINUX_INSTALLER} /${PETALINUX_INSTALLER}
RUN if [ -s /${PETALINUX_INSTALLER} ]; then \
    chmod a+x /${PETALINUX_INSTALLER}; \
    echo "PetaLinux installer found. Will proceed with installation."; \
else \
    echo "ERROR: PetaLinux installer is missing or empty."; \
    echo "Please download ${PETALINUX_INSTALLER} from AMD/Xilinx:"; \
    echo "https://www.xilinx.com/support/download/index.html/content/xilinx/en/downloadNav/embedded-design-tools/archive.html"; \
    echo "Go to Petalinux -> Archive -> ${PETALINUX_VERSION} -> PetaLinux Installer"; \
    exit 1; \
fi
COPY ./non-interactive-install.sh ./noninteractive-install.sh
RUN chmod a+x ./noninteractive-install.sh

# Ensure proper permissions for the user (make sure this is the last group of lines since the user will change for the docker)
RUN useradd -ms /bin/bash petalinux && echo "petalinux:petalinux" | chpasswd && adduser petalinux sudo && adduser petalinux dialout
USER petalinux
WORKDIR /home/petalinux

# Run noninteractive installer as non-root user per documenation to bypass Agreements page
RUN /./noninteractive-install.sh /home/petalinux/petalinux/ ${PETALINUX_VERSION} ${PETALINUX_RELEASE}

# Add Petalinux setup commands
RUN echo "source /home/petalinux/petalinux/settings.sh" >> ~/.bashrc

# Clean up files as root user
USER root
RUN rm /noninteractive-install.sh && rm /${PETALINUX_INSTALLER}

USER petalinux
# Source Petalinux settings on container startup and start a bash shell
CMD ["bash", "-c", "/etc/init.d/xinetd stop && /etc/init.d/xinetd start && source ~/.bashrc && exec bash"]
