variable "region" {
  description = "AWS region for every resource."
  type        = string
  default     = "us-east-1"
}

variable "name" {
  description = "Prefix for resource names."
  type        = string
  default     = "spot-runner"
}

variable "instance_types" {
  description = "Spot-eligible instance types. Several types across several AZs give the allocator more capacity pools, which lowers the interruption rate."
  type        = list(string)
  default     = ["c6i.large", "c6a.large", "c5.large", "c5a.large", "m6i.large", "m5.large"]
}

variable "desired_capacity" {
  description = "Number of spot workers. Set to 0 when idle to stop all compute spend."
  type        = number
  default     = 0
}

variable "max_size" {
  description = "Upper bound for the worker Auto Scaling group."
  type        = number
  default     = 4
}

variable "manager_image_tag" {
  description = "Tag of the manager image in ECR."
  type        = string
  default     = "latest"
}

variable "job_images" {
  description = "Job images (repository:tag in this account's ECR) to pre-pull on each worker."
  type        = list(string)
  default     = ["prime-counter:latest"]
}

variable "lease_duration_s" {
  description = "SQS visibility timeout used as the job lease."
  type        = number
  default     = 120
}
