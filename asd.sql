drop table good;
drop table person;
drop table sell;
CREATE TABLE good (
id varchar NOT NULL,
cost int,
max int,
remain int,
PRIMARY KEY (id)
);
CREATE TABLE person
(
id int NOT NULL,
password varchar,
pay_data int,
PRIMARY KEY (id)
);
CREATE TABLE sell
(
locale varchar NOT NULL,
id int ,
gid varchar,
num int,
y_m int,
PRIMARY KEY (locale,id,gid),
FOREIGN KEY (id) REFERENCES person(id),
FOREIGN KEY (gid) REFERENCES good(id)
);
INSERT INTO good VALUES ('locks', 45, 70,70);
INSERT INTO good VALUES ('stocks', 30, 80,80);
INSERT INTO good VALUES ('barrels', 25, 90,90);
INSERT INTO person VALUES (1, '1234', 0);
INSERT INTO person VALUES (2, 'qwer', 0);
INSERT INTO person VALUES (3, 'asdf', 0);
INSERT INTO person VALUES (4, 'zxcv', 0);
