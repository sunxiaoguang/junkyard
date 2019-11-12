USE test;

CREATE TABLE tweets (
  id BIGINT,
  time TIMESTAMP,
  user VARCHAR(256),
  content VARCHAR(512),
  polarity TINYINT,
  PRIMARY KEY(id),
  INDEX (user),
  INDEX (time)
);

INSERT INTO tweets(id, time, `user`, content, polarity) VALUES(1467810369,'2009-04-06 22:19:45','_a_unknown_user','@madeupuser http://twitpic.com/fake - awww, that\'s a bummer.  you shoulda got david carr of third day to do it. ;d',0);
