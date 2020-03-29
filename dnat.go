package main

import (
	"flag"
	"io"
	"log"
	"net"
	"strings"
	"sync"
	"time"
)

var servers = flag.String("servers", "", "real servers' address separated by comma")
var listen = flag.String("listen", ":1234", "listening address")

func tryConnect(target string) (net.Conn, error) {
	return net.Dial("tcp", target)
}

func dispatchTunnel(from, to net.Conn) {
	defer func() {
		to.Close()
		from.Close()
	}()
	disable := time.Now().Add(0xFFFF * time.Hour)
	from.SetDeadline(disable)
	from.SetReadDeadline(disable)
	from.SetWriteDeadline(disable)
	to.SetDeadline(disable)
	to.SetReadDeadline(disable)
	to.SetWriteDeadline(disable)
	buffer := make([]byte, 4096, 4096)
	for {
		if r, err := from.Read(buffer); err == nil {
			slice := buffer[:r]
			for {
				if w, err := to.Write(slice); err == nil {
					if r == w {
						break
					}
					slice = slice[w:]
					r -= w
				} else {
					if err == io.EOF {
						log.Printf("%v has closed connection, stop tunneling traffic from %v", to.RemoteAddr(), from.RemoteAddr())
					} else {
						log.Printf("Caught error '%v' when writing to %v, stop tunneling traffic from %v",
							err, to.RemoteAddr(), from.RemoteAddr())
					}
					return
				}
			}
		} else {
			if err == io.EOF {
				log.Printf("%v has closed connection, stop tunneling traffic to %v", from.RemoteAddr(), to.RemoteAddr())
			} else {
				log.Printf("Caught error '%v' when reading from %v, stop tunneling traffic to %v",
					err, from.RemoteAddr(), to.RemoteAddr())
			}
			return
		}
	}
}

func handle(conn net.Conn, target string) {
	backend, err := tryConnect(target)
	if err != nil {
		log.Printf("failed to connect to real server %s. %v", target, err)
		conn.Close()
		return
	}
	wg := &sync.WaitGroup{}
	wg.Add(2)
	go func() {
		defer wg.Done()
		dispatchTunnel(conn, backend)
	}()
	go func() {
		defer wg.Done()
		dispatchTunnel(backend, conn)
	}()
	wg.Wait()
}

func main() {
	flag.Parse()
	if len(*servers) == 0 {
		log.Fatal("no real servers")
	}
	realServers := strings.Split(*servers, ",")
	for _, server := range realServers {
		if c, err := tryConnect(server); err != nil {
			log.Fatalf("Could not connect to real server %s. %v", server, err)
		} else {
			c.Close()
		}
	}

	ln, err := net.Listen("tcp", *listen)
	if err != nil {
		log.Fatalf("Could not listen on %s. %v", *listen, err)
	}

	seed := 0
	for {
		conn, err := ln.Accept()
		if err != nil {
			log.Printf("Failed to accept: %v", err)
			continue
		}
		go handle(conn, realServers[seed%len(realServers)])
		seed++
	}
}
