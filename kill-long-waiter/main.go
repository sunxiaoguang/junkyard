package main

import (
	"database/sql"
	"flag"
	"fmt"
	"os"
	"sort"
	"strconv"
	"strings"
	"time"

	_ "github.com/go-sql-driver/mysql"
)

const (
	serverStatusInTrans = 0x0001
)

var (
	dsns       = flag.String("dsn", "root:@tcp(127.0.0.1:4000)/mysql", "connect dsn")
	signature  = flag.String("signature", "", "signature to match the query")
	count      = flag.Int("count", 1, "number of queries to kill")
	minTime    = flag.Duration("min-wait-time", 25*time.Second, "do not kill session wait time less than this")
	log        = flag.String("log", "kill.log", "kill log file")
	signatures []string
	sessions   []Session
	killed     []Session
	ds         []string
	logFile    *os.File
	startTime  = time.Now().Format("20060102150405")
)

type Session struct {
	id      int
	user    sql.NullString
	host    sql.NullString
	db      sql.NullString
	command sql.NullString
	time    int
	state   sql.NullString
	info    sql.NullString

	dsn    string
	status string
}

func (s Session) String() string {
	return fmt.Sprintf("ID: %d, User: '%s', Host: '%s', DB: '%s', Command: '%s', Time: %d, State: %s, Info: '%s', Status: '%s'",
		s.id,
		s.user.String,
		s.host.String,
		s.db.String,
		s.command.String,
		s.time,
		s.state.String,
		s.info.String,
		s.status)
}

func init() {
	flag.Parse()
	ds = strings.Split(*dsns, ";")
	if len(*signature) == 0 {
		signatures = make([]string, 0)
	} else {
		signatures = strings.Split(*signature, ";")
	}
	if f, err := os.OpenFile(*log, os.O_APPEND|os.O_RDWR|os.O_CREATE, 0600); err != nil {
		panic(err)
	} else {
		logFile = f
	}
}

func matchSignature(s string) bool {
	if len(signatures) == 0 {
		return true
	}
	for _, sig := range signatures {
		if strings.Contains(s, sig) {
			return true
		}
	}
	return false
}

func ensureNullString(s sql.NullString) sql.NullString {
	if !s.Valid {
		s.Valid = true
		s.String = "NULL"
	}
	return s
}

func ensureUint(s sql.NullString) uint64 {
	n, err := strconv.ParseUint(ensureNullString(s).String, 10, 64)
	if err != nil {
		n = 0
	}
	return n
}

func collect() {
	fmt.Fprintf(logFile, "============== Starting run-%s ==============\n", startTime)
	fmt.Fprintln(logFile, "************** Collect Processlist **************")
	num := 0
	for _, d := range ds {
		db, err := sql.Open("mysql", d)
		MustNil(err)
		defer db.Close()

		rows, err := db.Query("SHOW PROCESSLIST")
		MustNil(err)

		for rows.Next() {
			s := Session{}
			if err := rows.Scan(&s.id, &s.user, &s.host, &s.db, &s.command, &s.time, &s.state, &s.info); err != nil {
				// Warn but ignore error
				fmt.Fprintf(logFile, "WARN: error scanning result from processlist - %s", err.Error())
			}
			if ensureUint(s.state)&serverStatusInTrans != serverStatusInTrans {
				s.status = "NOT_IN_TRANS"
			} else if time.Duration(s.time)*time.Second < *minTime {
				s.status = "DID_NOT_WAIT_TOO_LONG"
			} else if ensureNullString(s.command).String != "Query" {
				s.status = "NOT_WAITING"
			} else {
				s.info = ensureNullString(s.info)
				if matchSignature(s.info.String) {
					s.dsn = d
					s.status = "MATCHED"
					sessions = append(sessions, s)
				} else {
					s.status = "NOT_MATCHED"
				}
			}
			fmt.Fprintln(logFile, s)
			num += 1
		}
	}
	fmt.Fprintf(logFile, "Total number of connections: %d\n", num)
	fmt.Fprintln(logFile, "************** Candidate Processlist **************")
	for _, s := range sessions {
		fmt.Fprintln(logFile, s)
	}
	fmt.Fprintf(logFile, "Total number of candidate connections: %d\n", len(sessions))
}

func sortByTime() {
	if *count < len(sessions) {
		sort.SliceStable(sessions, func(i, j int) bool {
			// Session with higher time will be killed.
			return sessions[i].time > sessions[j].time
		})
		killed = sessions[:*count]
	} else {
		killed = sessions
	}
}

func kill() {
	fmt.Fprintln(logFile, "************** Kill Processlist **************")
	ds := strings.Split(*dsns, ";")
	for _, d := range ds {
		db, err := sql.Open("mysql", d)
		MustNil(err)
		defer db.Close()

		for i := 0; i < len(killed); i++ {
			s := &killed[i]
			fmt.Fprintf(logFile, "%s => %s\n", s.dsn, d)
			if s.dsn != d {
				continue
			}
			_, err := db.Exec(fmt.Sprintf("KILL TIDB %d", s.id))
			MustNil(err)
			s.status = "KILLED"
			fmt.Fprintln(logFile, s)
		}
	}
	fmt.Fprintf(logFile, "Total number of killed connections: %d\n", len(killed))
}

func printTail() {
	fmt.Fprintf(logFile, "============== Finished run-%s ==============\n", startTime)
	logFile.Close()
}

func MustNil(i interface{}) {
	if i != nil {
		panic(fmt.Sprintf("%v not nil", i))
	}
}
