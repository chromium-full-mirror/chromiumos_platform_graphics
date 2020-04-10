package utils

import (
	"context"
	"crypto/md5"
	"encoding/hex"
	"os"
)

func GetFileMD5Sum(ctx context.Context, fileName string) (string, error) {
	file, err := os.Open(fileName)
	if err != nil {
		return "", err
	}
	defer file.Close()
	hash := md5.New()
	if err = CopyWithContext(ctx, hash, file); err != nil {
		return "", err
	}
	hashInBytes := hash.Sum(nil)[:16]
	return hex.EncodeToString(hashInBytes), nil
}
