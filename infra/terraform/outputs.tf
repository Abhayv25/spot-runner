output "checkpoint_bucket" {
  value = aws_s3_bucket.checkpoints.bucket
}

output "queue_url" {
  value = aws_sqs_queue.jobs.url
}

output "dead_letter_queue_url" {
  value = aws_sqs_queue.dead_letter.url
}

output "manager_repository_url" {
  value = aws_ecr_repository.manager.repository_url
}

output "prime_counter_repository_url" {
  value = aws_ecr_repository.prime_counter.repository_url
}

output "autoscaling_group" {
  value = aws_autoscaling_group.workers.name
}

output "fis_template_id" {
  value = aws_fis_experiment_template.spot_interruption.id
}
