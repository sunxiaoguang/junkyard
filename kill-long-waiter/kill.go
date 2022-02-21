package main

import (
	"fmt"
	"os"
	"strings"
)

func main() {
	logFile = os.Stdout
	collect()
	sortByTime()
	if askForConfirmation() {
		kill()
	}
	printTail()
}

func askForConfirmation() bool {
	fmt.Println("Do you want to kill candidate connections:")
	var response string

	_, err := fmt.Scanln(&response)
	if err != nil {
		panic(err)
	}

	switch strings.ToLower(response) {
	case "y", "yes":
		return true
	case "n", "no":
		return false
	default:
		fmt.Println("I'm sorry but I didn't get what you meant, please type (y)es or (n)o and then press enter:")
		return askForConfirmation()
	}
}
