package main

import (
	"context"
	"database/sql"
	"flag"
	"fmt"
	"time"

	_ "github.com/go-sql-driver/mysql"
)

var (
	followerRead = flag.Bool("follower", true, "Use follower read")
	dsn          = flag.String("tidb-dsn", "root@tcp(127.0.0.1:4000)/test", "TiDB Data Source Name")
	concurrency  = flag.Int("concurrency", 128, "default concurrency")

	finished = make(chan int, 16)
)

func stress() {
	db, err := sql.Open("mysql", *dsn)
	if err != nil {
		panic(err)
	}
	defer db.Close()
	ctx := context.Background()
	conn, err := db.Conn(ctx)
	if err != nil {
		panic(err)
	}
	defer conn.Close()
	var values []sql.RawBytes
	var scanValues []interface{}
	if _, err = conn.ExecContext(ctx, "SET autocommit=1"); err != nil {
		panic(err)
	}
	if *followerRead {
		if _, err = conn.ExecContext(ctx, "SET tidb_replica_read=follower"); err != nil {
			panic(err)
		}
	}
	for {
		rows, err := conn.QueryContext(ctx, "select * from tweets where id = 1467810369 and time < '2019-11-12 01:09:10'")
		if err != nil {
			panic(err)
		}
		if scanValues == nil {
			c, err := rows.Columns()
			if err != nil {
				panic(err)
			}
			values = make([]sql.RawBytes, len(c))
			scanValues = make([]interface{}, len(c))
			for i := range values {
				scanValues[i] = &values[i]
			}
		}
		for rows.Next() {
			if err = rows.Scan(scanValues...); err != nil {
				panic(err)
			}
		}
		rows.Close()
		finished <- 0
	}
}

func main() {
	flag.Parse()

	for i := 0; i < *concurrency; i++ {
		go stress()
	}

	mode := "follower read"
	if !*followerRead {
		mode = "leader read"
	}
	start := time.Now().UnixNano()
	var counter int64 = 0
	for _ = range finished {
		counter += 1
		if counter%1000 == 0 {
			fmt.Printf("%s: %d QPS\n", mode, counter*1000000000/(time.Now().UnixNano()-start))
			counter = 0
			start = time.Now().UnixNano()
		}
	}
}
