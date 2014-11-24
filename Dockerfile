# Dockerfile 
  
FROM debian:wheezy

MAINTAINER mk
#update and install prerequisites/compilation stuff
RUN apt-get update -y
RUN apt-get install make gcc zlib1g-dev -y
ADD source /source
RUN chmod 777 source/ -R
#compilation steps
RUN ./source/configure --with-openssl=none
RUN cd /source & make
# copying app to path
RUN cp fossil /bin/
#remove source code
RUN rm -rf /source
# Set workdir and start server
WORKDIR /fossilapp
RUN fossil new --empty -A admin tzrepository.fossil
RUN fossil user password -R tzrepository.fossil admin techzone
RUN fossil cache init -R tzrepository.fossil


ENTRYPOINT fossil server tzrepository.fossil
EXPOSE 8080
# docker build -t fossilimage /home/fossil/
# docker run --name fossilcontainer -d -p 9090:8080 -t fossilimage