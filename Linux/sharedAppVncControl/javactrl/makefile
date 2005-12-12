all: javactrl.jar

javactrl.jar: javactrl.java
	javac ./*.java
	jar -cvfm javactrl.jar manifest.mf *.class


clean:
	rm javactrl.jar ./*.class
